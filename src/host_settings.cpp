#include "host_settings.h"

#include "host_gpu_live.h"
#include "host_keybinds.h"
#include "host_video_options.h"

#include <rex/cvar.h>
#include <rex/logging/api.h>
#include <rex/logging/macros.h>
#include <rex/rex_app.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace nba_settings {
namespace {

std::set<std::string> g_cli;              // flags given on the command line
std::set<std::string> g_hand;             // raw cvars set by hand in settings.ini
std::map<std::string, std::string> g_orig;  // emulator defaults of the scale fixes
std::vector<std::string> g_log;           // messages queued before logging is up
bool g_log_ready = false;
bool g_big_picture = false;
std::filesystem::path g_missing_default_xex;

void Note(const std::string& s) {
  if (g_log_ready) {
    REXLOG_INFO("settings: {}", s);
  } else {
    g_log.push_back(s);
  }
}

bool EqI(const std::string& a, const char* b) {
#ifdef _WIN32
  return _stricmp(a.c_str(), b) == 0;
#else
  return strcasecmp(a.c_str(), b) == 0;
#endif
}

bool LaunchedInBigPicture() {
#ifdef _WIN32
  wchar_t mode[24];
  const DWORD size = GetEnvironmentVariableW(L"RECOMPLAUNCHER_MODE", mode, _countof(mode));
  return size > 0 && size < _countof(mode) && _wcsicmp(mode, L"bigpicture") == 0;
#else
  const char* mode = std::getenv("RECOMPLAUNCHER_MODE");
  return mode && EqI(mode, "bigpicture");
#endif
}

std::filesystem::path ExeDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
  if (n > 0 && n < sizeof(buf) / sizeof(buf[0])) {
    return std::filesystem::path(buf).parent_path();
  }
#endif
  return std::filesystem::current_path();
}

std::filesystem::path SettingsDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 2];
  const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
  if (n > 0 && n < sizeof(buf) / sizeof(buf[0])) {
    return std::filesystem::path(buf) / HOST_SETTINGS_DIR_NAME;
  }
#endif
  return ExeDir() / "settings";
}

std::string IniPath() { return (SettingsDir() / HOST_SETTINGS_FILE_NAME).string(); }

bool CvarExists(const std::string& name) { return rex::cvar::GetFlagInfo(name) != nullptr; }

// A set/default that could not be applied yet because the cvar is defined in
// the GPU plugin DLL, which the SDK only loads after OnConfigurePaths.
struct Pending {
  std::string name;
  std::string value;
  bool is_default;
};
std::vector<Pending> g_pending;
bool g_scale_fix_pending = false;

// Apply `value` to `name` unless the command line owns it (or, for a built-in
// default, anything but the compiled-in default already set it). Unknown cvars
// are queued and retried by FlushPending().
bool Put(const std::string& name, const std::string& value, bool is_default) {
  const auto* info = rex::cvar::GetFlagInfo(name);
  if (!info) {
    g_pending.push_back({name, value, is_default});
    return false;
  }
  // Big Picture is a session override, including edits made while playing.
  if (g_big_picture && (name == "fullscreen" || name == "fullscreen_exclusive")) {
    return rex::cvar::SetFlagByName(name, name == "fullscreen" ? "true" : "false");
  }
  if (g_cli.count(name) || info->source == rex::cvar::Source::kCommandLine) {
    return false;  // explicit command-line flag always wins
  }
  if (is_default && info->source != rex::cvar::Source::kDefault) {
    return false;  // env/config file already chose a value
  }
  if (!rex::cvar::SetFlagByName(name, value)) {
    Note("rejected " + name + "=" + value);
    return false;
  }
  return true;
}

bool SetCvar(const std::string& name, const std::string& value) { return Put(name, value, false); }
void SetDefault(const std::string& name, const std::string& value) { Put(name, value, true); }
void ApplyScaleFixes() {
  static const char* const kFixes[2] = {"draw_resolution_scaled_texture_offsets",
                                        "resolve_resolution_scale_fill_half_pixel_offset"};
  if (!CvarExists("resolution_scale")) {
    g_scale_fix_pending = true;  // GPU plugin not loaded yet; FlushPending() retries
    return;
  }
  g_scale_fix_pending = false;
  int scale = std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str());
  if (scale < 1) {
    scale = 1;
  }
  for (const char* fix : kFixes) {
    if (g_hand.count(fix) || g_cli.count(fix) || !CvarExists(fix)) {
      continue;
    }
    if (!g_orig.count(fix)) {
      g_orig[fix] = rex::cvar::GetFlagByName(fix);
    }
    SetCvar(fix, scale > 1 ? std::string("true") : g_orig[fix]);
  }
}

std::vector<std::pair<std::string, std::string>> ReadIniSection(const char* section,
                                                                const std::string& path) {
  std::vector<std::pair<std::string, std::string>> out;
#ifdef _WIN32
  std::vector<char> buf(32768);
  const DWORD n = GetPrivateProfileSectionA(section, buf.data(), static_cast<DWORD>(buf.size()),
                                            path.c_str());
  size_t i = 0;
  while (i < n && buf[i]) {
    std::string line(&buf[i]);
    i += line.size() + 1;
    const size_t eq = line.find('=');
    if (eq == std::string::npos || eq == 0) {
      continue;
    }
    std::string k = line.substr(0, eq);
    std::string v = line.substr(eq + 1);
    while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) v.pop_back();
    out.emplace_back(std::move(k), std::move(v));
  }
#else
  (void)section;
  (void)path;
#endif
  return out;
}

}  // namespace

std::filesystem::path LogDir() { return SettingsDir() / "logs"; }

void ApplyVideoSetting(const char* key, const char* value) {
  if (!key || !value) {
    return;
  }
  if (HostDisplayModeIs(key)) {
    const char* fs = nullptr;
    const char* ex = nullptr;
    HostDisplayModeExpand(value, &fs, &ex);
    SetCvar("fullscreen", fs);
    SetCvar("fullscreen_exclusive", ex);
  } else if (!_stricmp(key, HOST_FRAME_RATE_KEY)) {
    const char* limit = nullptr;
    const char* refresh = nullptr;
    HostFrameRateExpand(value, &limit, &refresh);
    SetCvar("nba_frame_limit", limit);
    SetCvar("video_mode_refresh_rate", refresh);
  } else if (!_stricmp(key, HOST_ANTI_ALIASING_KEY)) {
    const char* swap_fx = nullptr;
    const char* msaa = nullptr;
    HostAntiAliasExpand(value, &swap_fx, &msaa);
    SetCvar("swap_post_effect", swap_fx);
    SetCvar("native_2x_msaa", msaa);
  } else if (!_stricmp(key, HOST_RESOLUTION_SCALE_KEY)) {
    SetCvar(key, value);
    ApplyScaleFixes();
  } else {
    SetCvar(key, value);
  }
}

std::string CurrentVideoValue(const char* key) {
  if (HostDisplayModeIs(key)) {
    return HostDisplayModeFromFlags(rex::cvar::GetFlagByName("fullscreen").c_str(),
                                    rex::cvar::GetFlagByName("fullscreen_exclusive").c_str());
  }
  if (!_stricmp(key, HOST_FRAME_RATE_KEY)) {
    const std::string limit = rex::cvar::GetFlagByName("nba_frame_limit");
    if (limit == "0") return "unlimited";
    if (limit == "30") return "30";
    return "60";
  }
  if (!_stricmp(key, HOST_ANTI_ALIASING_KEY)) {
    const std::string swap_fx = rex::cvar::GetFlagByName("swap_post_effect");
    if (EqI(swap_fx, "fxaa")) return "fxaa";
    if (EqI(swap_fx, "fxaa_extreme")) return "fxaa_extreme";
    return EqI(rex::cvar::GetFlagByName("native_2x_msaa"), "true") ? "msaa2x" : "off";
  }
  return rex::cvar::GetFlagByName(key);
}

namespace {
std::string g_boot_scale = "1";       // resolution_scale the GPU was created with
std::string g_boot_msaa = "false";    // native_2x_msaa the GPU was created with
bool g_boot_captured = false;

bool IsTrue(const std::string& s) { return EqI(s, "true") || s == "1" || EqI(s, "on"); }
}  // namespace

void CaptureBootGpuState() {
  g_boot_scale = rex::cvar::GetFlagByName("resolution_scale");
  if (g_boot_scale.empty()) g_boot_scale = "1";
  g_boot_msaa = IsTrue(rex::cvar::GetFlagByName("native_2x_msaa")) ? "true" : "false";
  g_boot_captured = true;
  REXLOG_INFO("settings: GPU boot state resolution_scale={} native_2x_msaa={}", g_boot_scale,
              g_boot_msaa);
}

bool VideoValueNeedsRestart(const char* key, const char* value) {
  if (!key || !value || !g_boot_captured) return false;
  if (!_stricmp(key, HOST_RESOLUTION_SCALE_KEY)) {
    return std::atoi(value) != std::atoi(g_boot_scale.c_str());
  }
  if (!_stricmp(key, HOST_ANTI_ALIASING_KEY)) {
    const char* swap_fx = nullptr;
    const char* msaa = nullptr;
    HostAntiAliasExpand(value, &swap_fx, &msaa);
    return g_boot_msaa != msaa;
  }
  return false;
}

VideoApply ApplyVideoSettingRuntime(const char* key, const char* value) {
  if (!key || !value) return VideoApply::kLive;
  if (!_stricmp(key, HOST_RESOLUTION_SCALE_KEY)) {
    // The texture cache / scaled-resolve buffers are sized once at GPU init;
    // flipping the cvar (or the scale fixes that depend on it) mid-run would
    // corrupt rendering, so only settings.ini changes.
    if (VideoValueNeedsRestart(key, value)) return VideoApply::kRestart;
    ApplyVideoSetting(key, value);
    return VideoApply::kLive;
  }
  if (!_stricmp(key, HOST_ANTI_ALIASING_KEY)) {
    const char* swap_fx = nullptr;
    const char* msaa = nullptr;
    HostAntiAliasExpand(value, &swap_fx, &msaa);
    // FXAA is a swap post-effect and switches live. 2x MSAA changes the render
    // target cache layout (created at GPU init), so it needs a restart.
    SetCvar("swap_post_effect", swap_fx);
    nba_gpu_live::SetSwapPostEffect(swap_fx);
    if (g_boot_msaa != msaa) return VideoApply::kRestart;
    SetCvar("native_2x_msaa", msaa);
    return VideoApply::kLive;
  }
  // present_effect (former Upscale & sharpen) is no longer a visible option
  // and is ignored when read from settings.ini (see HostIsIgnoredKey).
  // display_mode (fullscreen change callbacks), frame_rate (frame pacer),
  // vsync, anisotropic_override and present_letterbox are all read live.
  ApplyVideoSetting(key, value);
  return VideoApply::kLive;
}

std::string StoredVideoValue(const char* key) {
  if (g_big_picture && HostDisplayModeIs(key)) {
#ifdef _WIN32
    for (const auto& kv : ReadIniSection(HOST_SETTINGS_SECTION, IniPath())) {
      if (EqI(kv.first, key) && !kv.second.empty()) return kv.second;
    }
#endif
    return "borderless";
  }
  if (key && (!_stricmp(key, HOST_RESOLUTION_SCALE_KEY) || !_stricmp(key, HOST_ANTI_ALIASING_KEY))) {
#ifdef _WIN32
    const std::string path = IniPath();
    for (const auto& kv : ReadIniSection(HOST_SETTINGS_SECTION, path)) {
      if (EqI(kv.first, key) && !kv.second.empty()) return kv.second;
    }
#endif
  }
  return CurrentVideoValue(key);
}

void ApplyIni() {
#ifdef _WIN32
  const std::string path = IniPath();
  if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    ApplyScaleFixes();
    return;
  }
  HostIniMigrate(path.c_str());

  const auto display = ReadIniSection(HOST_SETTINGS_SECTION, path);
  std::set<std::string> visible;
  for (int i = 0; i < HOST_VIDEO_OPTS_COUNT; ++i) {
    visible.insert(kHostVideoOpts[i].cvar);
  }
  // 1) player-facing options (virtual keys expand to several cvars).
  for (int i = 0; i < HOST_VIDEO_OPTS_COUNT; ++i) {
    const HostVideoOpt& opt = kHostVideoOpts[i];
    for (const auto& kv : display) {
      if (!EqI(kv.first, opt.cvar) || kv.second.empty()) {
        continue;
      }
      const int idx = HostVideoFindChoice(&opt, kv.second.c_str());
      ApplyVideoSetting(opt.cvar, opt.values[idx]);
    }
  }
  // 2) hand-written raw cvar overrides (hidden/locked settings stay overridable).
  g_hand.clear();
  for (const auto& kv : display) {
    if (kv.second.empty() || EqI(kv.first, HOST_SETTINGS_SCHEMA_KEY) ||
        EqI(kv.first, "fullscreen") || EqI(kv.first, "fullscreen_exclusive")) {
      continue;
    }
    bool is_visible = false;
    for (const auto& v : visible) {
      if (EqI(kv.first, v.c_str())) is_visible = true;
    }
    if (is_visible) {
      continue;
    }
    // Obsolete keys (e.g. present_effect) are left in the file and skipped,
    // so a leftover Upscale & sharpen value cannot change the SDK default.
    if (HostIsIgnoredKey(kv.first.c_str())) {
      continue;
    }
    if (SetCvar(kv.first, kv.second)) {
      g_hand.insert(kv.first);
    }
  }
  // 3) automatic scale fixes (skips anything set by hand or on the command line).
  ApplyScaleFixes();
  // 4) key bindings.
  for (int i = 0; i < HOST_KEYBIND_COUNT; ++i) {
    char buf[192] = {};
    GetPrivateProfileStringA(HOST_KEYBINDS_SECTION, kHostKeybinds[i].cvar, "", buf, sizeof buf,
                             path.c_str());
    if (buf[0]) {
      SetCvar(kHostKeybinds[i].cvar, HostKeybindFromIni(buf));
    }
  }
#endif
}

void FlushPending() {
  auto todo = std::move(g_pending);
  g_pending.clear();
  for (const auto& p : todo) {
    Put(p.name, p.value, p.is_default);
  }
  if (g_scale_fix_pending) {
    ApplyScaleFixes();
  }
}

void ApplyStartup(rex::PathConfig& paths) {
  g_big_picture = LaunchedInBigPicture();
  g_cli.clear();
  for (const auto& e : rex::cvar::GetRegistry()) {
    if (e.source == rex::cvar::Source::kCommandLine) {
      g_cli.insert(e.name);
    }
  }
  {
    std::string names;
    for (const auto& n : g_cli) names += (names.empty() ? "" : " ") + n;
    Note("command-line flags: " + (names.empty() ? std::string("(none)") : names));
  }

  // --- roots: exe-relative game folder, per-user data under LOCALAPPDATA ---
  const std::filesystem::path base = SettingsDir();
  std::error_code ec;
  std::filesystem::create_directories(base, ec);
  // A root counts as "explicit" when the command line (or config file) set the
  // matching cvar; otherwise our defaults replace whatever the SDK derived.
  auto explicit_flag = [](const char* name) {
    const auto* info = rex::cvar::GetFlagInfo(name);
    return info && info->source != rex::cvar::Source::kDefault;
  };
  Note("sdk path defaults: game=" + paths.game_data_root.string() +
       " user=" + paths.user_data_root.string() + " cache=" + paths.cache_root.string() +
       " metadata=" + paths.metadata_root.string());
  bool game_defaulted = false;
  if (paths.game_data_root.empty() || !explicit_flag("game_data_root")) {
    paths.game_data_root = ExeDir() / "game";
    game_defaulted = true;
    SetCvar("game_data_root", paths.game_data_root.string());
  }
  if (paths.user_data_root.empty() || !explicit_flag("user_data_root")) {
    paths.user_data_root = base / "user";
    SetCvar("user_data_root", paths.user_data_root.string());
  }
  if (paths.cache_root.empty() || !explicit_flag("cache_root")) {
    paths.cache_root = base / "cache";
    SetCvar("cache_root", paths.cache_root.string());
  }
  if (paths.metadata_root.empty() || !explicit_flag("metadata_root")) {
    paths.metadata_root = base / "metadata";
    SetCvar("metadata_root", paths.metadata_root.string());
  }  for (const auto* p : {&paths.user_data_root, &paths.cache_root, &paths.metadata_root}) {
    std::filesystem::create_directories(*p, ec);
  }
  Note("game_data_root=" + paths.game_data_root.string());

#ifdef _WIN32
  if (game_defaulted &&
      !std::filesystem::is_regular_file(paths.game_data_root / "default.xex", ec)) {
    g_missing_default_xex = paths.game_data_root / "default.xex";
  }
#endif

  // The GPU plugin DLL defines most video cvars (vsync, resolution_scale, ...)
  // and is normally loaded later. Load it now (the SDK reuses the same module)
  // so those cvars exist, and command-line values for them are visible.
#ifdef _WIN32
  if (!CvarExists("vsync")) {
    const std::string plugin = rex::cvar::GetFlagByName("gpu_plugin");
    if (plugin.empty() || plugin == "xenos") {
      const auto dll = ExeDir() / "rexgpu-xenos.dll";
      if (std::filesystem::exists(dll, ec) && LoadLibraryW(dll.wstring().c_str())) {
        Note("loaded rexgpu-xenos.dll early to register video cvars");
      }
    }
  }
#endif
  // Re-snapshot command-line ownership now that the plugin cvars exist.
  for (const auto& e : rex::cvar::GetRegistry()) {
    if (e.source == rex::cvar::Source::kCommandLine) {
      g_cli.insert(e.name);
    }
  }

  // --- built-in defaults (the old shortcut's flags, with 60 fps) ---
  SetDefault("gpu_plugin", "xenos");
  SetDefault("nba_force_full_license", "true");
  SetDefault("license_mask", "0xFFFFFFFF");
  SetDefault("nba_disable_live_menu", "true");
  SetDefault("mnk_mode", "true");
  // The frame pacer (nba_frame_limit) is the cap; guest vsync stays off.
  SetDefault("vsync", "false");
  // First boot / no display_mode in settings.ini: Borderless fullscreen, not
  // the SDK's Windowed default. settings.ini and the command line still win.
  SetDefault("fullscreen", "true");
  SetDefault("fullscreen_exclusive", "false");
  SetDefault("video_mode_refresh_rate", "60");
  SetDefault("nba_frame_limit", "60");
  for (int i = 0; i < HOST_HIDDEN_COUNT; ++i) {
    if (kHostHiddenOpts[i].locked) {
      SetDefault(kHostHiddenOpts[i].cvar, kHostHiddenOpts[i].locked);
    }
  }

  // --- settings.ini (config.exe / F1) beats the defaults, loses to the CLI ---
  ApplyIni();
  FlushPending();
  if (g_big_picture) Note("RecompLauncher Big Picture: forced borderless fullscreen for this session");
}

void FlushStartupLog() {
  // ReXGlue reads its own TOML after OnConfigurePaths, before it creates the window.
  if (g_big_picture) ApplyVideoSetting(HOST_DISPLAY_MODE_CVAR, "borderless");
  g_log_ready = true;
  for (const auto& p : g_pending) {
    g_log.push_back("not applied yet (cvar not registered): " + p.name + "=" + p.value);
  }
  for (const auto& s : g_log) {
    REXLOG_INFO("settings: {}", s);
  }
  g_log.clear();
  REXLOG_INFO(
      "settings: effective vsync={} frame_limit={} refresh={} resolution_scale={} fullscreen={} "
      "excl={} msaa2x={} swap_fx={} present_effect={} aniso={} letterbox={} scale_tex_fix={} "
      "scale_fill_fix={}",
      rex::cvar::GetFlagByName("vsync"), rex::cvar::GetFlagByName("nba_frame_limit"),
      rex::cvar::GetFlagByName("video_mode_refresh_rate"),
      rex::cvar::GetFlagByName("resolution_scale"), rex::cvar::GetFlagByName("fullscreen"),
      rex::cvar::GetFlagByName("fullscreen_exclusive"),
      rex::cvar::GetFlagByName("native_2x_msaa"), rex::cvar::GetFlagByName("swap_post_effect"),
      rex::cvar::GetFlagByName("present_effect"),
      rex::cvar::GetFlagByName("anisotropic_override"),
      rex::cvar::GetFlagByName("present_letterbox"),
      rex::cvar::GetFlagByName("draw_resolution_scaled_texture_offsets"),
      rex::cvar::GetFlagByName("resolve_resolution_scale_fill_half_pixel_offset"));
}

void ReportMissingGameFiles() {
#ifdef _WIN32
  if (g_missing_default_xex.empty()) return;
  REXLOG_ERROR("Startup: game files missing (expected game/default.xex beside the executable)");
  rex::FlushLogging();
  if (rex::cvar::GetFlagByName("headless") != "true") {
    const std::wstring msg = L"The game files are missing.\n\nExpected: " +
        g_missing_default_xex.wstring() +
        L"\n\nRun Setup again and select your game ZIP or extracted folder.";
    MessageBoxW(nullptr, msg.c_str(), L"NBA Unrivaled", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
  }
  ExitProcess(1);
#endif
}

}  // namespace nba_settings
