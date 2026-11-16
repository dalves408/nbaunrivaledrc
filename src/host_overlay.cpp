// ESC quit prompt + F1 settings overlay.
// UX/layout follows NHL Hitz Pro (orange frame, SETTINGS rows, Close/Quit).
// The video option list is shared with config.exe (host_video_options.h) and
// applied through host_settings.cpp, so both always match.
//
// Sizing: every size here (rows, fonts, panels) is derived from the actual
// window/swapchain output size and DPI, never from the internal render
// resolution, so Low / Medium / High look identical.

#include "host_overlay.h"

#include "host_keybinds.h"
#include "host_pause.h"
#include "host_restart.h"
#include "host_settings.h"
#include "host_video_options.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/logging/macros.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
// ReXGlue 0.10.1 refreshes the same device vectors from both guest input
// imports and GetStateForUI. Serialize the title's imports with our overlay.
std::mutex g_input_mutex;

PPCFunc* GetSdkInputImport(const char* name) {
  const auto runtime = GetModuleHandleW(L"rexruntime.dll");
  auto* target = runtime ? reinterpret_cast<PPCFunc*>(GetProcAddress(runtime, name)) : nullptr;
  if (!target) REXLOG_ERROR("Input: ReXGlue export {} is missing", name);
  return target;
}

void CallSdkInputImport(PPCFunc* target, PPCContext& ctx, uint8_t* base) {
  if (!target) {
    using rex::X_RESULT;
    ctx.r3.u64 = X_ERROR_DEVICE_NOT_CONNECTED;
    return;
  }
  const std::lock_guard lock(g_input_mutex);
  target(ctx, base);
}
#endif

constexpr ImU32 kAccent = IM_COL32(255, 122, 0, 255);
constexpr ImU32 kPanel = IM_COL32(14, 22, 40, 255);
constexpr ImU32 kRowOff = IM_COL32(52, 60, 78, 255);
constexpr ImU32 kTxt = IM_COL32(200, 210, 225, 255);
constexpr ImU32 kTxtSel = IM_COL32(10, 10, 14, 255);
constexpr ImU32 kHint = IM_COL32(150, 162, 182, 255);
constexpr ImU32 kDim = IM_COL32(0, 0, 0, 150);

enum class OverlayMode { kNone, kQuit, kSettings, kKeys, kRestart };

/* Settings rows: video options, Redefine Keys, Close, Quit. */
constexpr int kSettingsExtraRows = 3;
constexpr int kRowRedefineKeys = 0; /* offset after HOST_VIDEO_OPTS_COUNT */
constexpr int kRowClose = 1;
constexpr int kRowQuit = 2;

/* Keys screen rows: [0] Rebind all keys, [1..N] inputs, [N+1] Back. */
constexpr int kKeysTopRows = 1;
constexpr int kKeysRows = kKeysTopRows + HOST_KEYBIND_COUNT + 1;
constexpr int kKeysBackRow = kKeysTopRows + HOST_KEYBIND_COUNT;

/* Key capture state machine (edge-triggered). */
enum class CapState {
  kIdle,         // not capturing
  kWaitRelease,  // slot picked; waiting until every key is up
  kArmed         // listening for ONE key-down edge
};

bool EqualsIgnoreCase(const char* a, const char* b) {
#ifdef _WIN32
  return _stricmp(a, b) == 0;
#else
  return strcasecmp(a, b) == 0;
#endif
}

bool IniDir(char* out, size_t n) {
#ifdef _WIN32
  char base[MAX_PATH];
  if (!GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof base)) {
    return false;
  }
  std::snprintf(out, n, "%s\\%s", base, HOST_SETTINGS_DIR_NAME);
  CreateDirectoryA(out, nullptr);
  return true;
#else
  (void)out;
  (void)n;
  return false;
#endif
}

bool IniPath(char* out, size_t n) {
  char dir[512];
  if (!IniDir(dir, sizeof dir)) {
    return false;
  }
  std::snprintf(out, n, "%s\\%s", dir, HOST_SETTINGS_FILE_NAME);
  return true;
}

void IniPutSection(const char* section, const char* key, const char* value) {
#ifdef _WIN32
  char path[MAX_PATH];
  if (!IniPath(path, sizeof path)) {
    return;
  }
  WritePrivateProfileStringA(section, key, value, path);
#else
  (void)section;
  (void)key;
  (void)value;
#endif
}

/* Write one video choice. Cleans up an older settings.ini first and stamps the
 * schema marker so hidden/legacy keys cannot override the locked defaults. */
void IniPutVideo(const char* key, const char* value) {
#ifdef _WIN32
  char path[MAX_PATH];
  if (!IniPath(path, sizeof path)) {
    return;
  }
  HostIniMigrate(path);
  WritePrivateProfileStringA(HOST_SETTINGS_SECTION, HOST_SETTINGS_SCHEMA_KEY,
                             HOST_SETTINGS_SCHEMA_VALUE, path);
  WritePrivateProfileStringA(HOST_SETTINGS_SECTION, key, value, path);
#else
  (void)key;
  (void)value;
#endif
}

std::string IniGetSection(const char* section, const char* key, const char* fallback) {
#ifdef _WIN32
  char path[MAX_PATH];
  char buf[256];
  if (!IniPath(path, sizeof path)) {
    return fallback ? fallback : "";
  }
  GetPrivateProfileStringA(section, key, fallback ? fallback : "", buf, sizeof buf, path);
  return buf;
#else
  (void)section;
  (void)key;
  return fallback ? fallback : "";
#endif
}

class HostOverlayDialog final : public rex::ui::ImGuiDialog,
                                public rex::ui::WindowInputListener {
 public:
  HostOverlayDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window)
      : ImGuiDialog(drawer), window_(window) {
    sel_.assign(HOST_VIDEO_OPTS_COUNT, 0);
    applied_sel_.assign(HOST_VIDEO_OPTS_COUNT, 0);
    for (int i = 0; i < HOST_VIDEO_OPTS_COUNT; ++i) {
      sel_[i] = kHostVideoOpts[i].def;
    }
    key_vals_.assign(HOST_KEYBIND_COUNT, "");
    for (int i = 0; i < HOST_KEYBIND_COUNT; ++i) {
      key_vals_[i] = kHostKeybinds[i].def;
    }
    RefreshFromCvars();
    RefreshKeybindsFromCvars();
    // Also register SDK binds (F4-style) so they appear in the cvar settings UI.
    rex::ui::RegisterBind("bind_nba_host_settings", "F1",
                          "Toggle NBA Unrivaled host video settings (Hitz-style)",
                          [this] { if (!BindBlocked()) ToggleSettings(); });
    rex::ui::RegisterBind("bind_nba_host_quit", "Escape",
                          "Open NBA Unrivaled quit prompt (Hitz-style)",
                          [this] { if (!BindBlocked()) ToggleQuit(); });
    if (window_) {
      // High z-order so we see host keys even when MNK owns the guest pad.
      window_->AddInputListener(this, 100000);
    }
    Note("ctor");
  }

  ~HostOverlayDialog() override {
    SetPadInputBlocked(false);
    rex::ui::UnregisterBind("bind_nba_host_settings");
    rex::ui::UnregisterBind("bind_nba_host_quit");
    if (window_) {
      window_->RemoveInputListener(this);
    }
    if (imgui_drawer()) {
      imgui_drawer()->RemoveDialog(this);
    }
  }

  void ApplyIniFile() {
    nba_settings::ApplyIni();
    RefreshFromCvars();
    RefreshKeybindsFromCvars();
  }

 protected:
  bool GameIsForeground() const {
#ifdef _WIN32
    if (!window_) {
      return false;
    }
    void* native = window_->GetNativeWindowHandle();
    if (!native) {
      return false;
    }
    return GetForegroundWindow() == static_cast<HWND>(native);
#else
    return true;
#endif
  }

  void PollHostHotkeys() {
#ifdef _WIN32
    // SDL often drops synthetic keybd_event for WindowInputListener here.
    // Edge-detect via GetAsyncKeyState. Prefer our HWND foreground; also accept
    // when our process owns the foreground window (HardFocus races).
    HWND native = window_ ? static_cast<HWND>(window_->GetNativeWindowHandle()) : nullptr;
    HWND fg = GetForegroundWindow();
    DWORD fg_pid = 0, self = GetCurrentProcessId();
    if (fg) {
      GetWindowThreadProcessId(fg, &fg_pid);
    }
    const bool ours = (native && fg == native) || (fg_pid == self);
    const bool f1 = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
    const bool esc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    // SDL can miss F1 and Esc when the window is changing focus.
    if (ours || f1 || esc) {
      if (f1 && !f1_down_) ToggleSettings();
      if (esc && !esc_down_) ToggleQuit();
    }
    f1_down_ = f1;
    esc_down_ = esc;
    // Capture runs last: it uses its own edge detection over every key.
    if (mode_ == OverlayMode::kKeys && key_wait_ >= 0) {
      CaptureStep(ours);
    } else {
      cap_state_ = CapState::kIdle;
    }
#endif
  }

  static rex::input::InputSystem* InputSystem() {
    auto* runtime = rex::Runtime::instance();
    return runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
  }

  void SetPadInputBlocked(bool blocked) {
    if (pad_input_blocked_ == blocked) return;
    if (auto* input = InputSystem()) {
#ifdef _WIN32
      const std::lock_guard lock(g_input_mutex);
#endif
      if (blocked) input->AddUIInputBlocker();
      else input->RemoveUIInputBlocker();
      pad_input_blocked_ = blocked;
    }
  }

  void PollController() {
    auto* input = InputSystem();
    pad_pressed_ = 0;
    if (!input || !window_ || !window_->HasFocus()) {
      pad_buttons_.fill(0);
      return;
    }
    bool toggle = false;
    {
#ifdef _WIN32
      const std::unique_lock lock(g_input_mutex, std::try_to_lock);
      if (!lock.owns_lock()) return;
#endif
      for (uint32_t user = 0; user < pad_buttons_.size(); ++user) {
        rex::input::X_INPUT_STATE state{};
        input->GetStateForUI(user, &state);
        const uint16_t buttons = state.gamepad.buttons;
        const uint16_t previous = pad_buttons_[user];
        const uint16_t pressed = buttons & ~previous;
        pad_buttons_[user] = buttons;
        pad_pressed_ |= pressed;
        toggle |= (previous & rex::input::X_INPUT_GAMEPAD_BACK) &&
                  (buttons & rex::input::X_INPUT_GAMEPAD_BACK) &&
                  (pressed & rex::input::X_INPUT_GAMEPAD_START);
      }
    }
    if (toggle && !BindBlocked()) {
      ToggleSettings("Back+Start");
      pad_pressed_ = 0;
    }
  }

  bool PadPressed(uint16_t buttons) const { return (pad_pressed_ & buttons) != 0; }

  void OnDraw(ImGuiIO& io) override {
    PollHostHotkeys();
    PollController();
    SetPadInputBlocked(mode_ != OverlayMode::kNone);
    // Any open menu (F1 settings, key mapper, Esc quit) holds the game: guest
    // Present stalls and audio pauses; this overlay keeps drawing and taking input.
    nba_pause::Set(mode_ != OverlayMode::kNone);
    if (mode_ == OverlayMode::kNone) {
      DrawNotice(io);
      return;
    }
    io.WantCaptureKeyboard = true;
    io.WantCaptureMouse = true;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ComputeUiScale(vp);
    if (!logged_scale_) {
      logged_scale_ = true;
      LogScale(vp, io);
    }
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##nba_host_overlay_root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(vp->WorkPos,
                      ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y),
                      kDim);

    if (mode_ == OverlayMode::kQuit || mode_ == OverlayMode::kRestart) {
      DrawQuit(dl, vp);
      HandleQuitKeys();
    } else if (mode_ == OverlayMode::kKeys) {
      DrawKeys(dl, vp);
      HandleKeysKeys();
    } else {
      DrawSettings(dl, vp);
      HandleSettingsKeys();
    }

    SetPadInputBlocked(mode_ != OverlayMode::kNone);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
  }

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.prev_state()) {
      return;  // ignore auto-repeat
    }
    const auto vk = e.virtual_key();
    if (key_wait_ >= 0 && mode_ == OverlayMode::kKeys) {
      // Capture is edge-detected in CaptureStep (GetAsyncKeyState); just keep
      // the key away from the game and ImGui.
      e.set_handled(true);
      return;
    }
    if (mode_ == OverlayMode::kNone) return;
    e.set_handled(true);
    using Key = rex::ui::VirtualKey;
    if (vk == Key::kF1) {
      ToggleSettings();
    } else if (vk == Key::kEscape) {
      ToggleQuit();
    } else {
      HandleNavigationKey(vk);
    }
  }

 private:
  void HandleNavigationKey(rex::ui::VirtualKey key) {
    using Key = rex::ui::VirtualKey;
    const bool confirm = key == Key::kReturn || key == Key::kSpace;
    const int direction = (key == Key::kRight || key == Key::kD) -
                          (key == Key::kLeft || key == Key::kA);
    const int row_delta = (key == Key::kDown || key == Key::kS) -
                          (key == Key::kUp || key == Key::kW);
    if (mode_ == OverlayMode::kQuit || mode_ == OverlayMode::kRestart) {
      if (direction) q_sel_ = direction > 0;
      if (confirm) ConfirmDialogChoice();
    } else if (mode_ == OverlayMode::kKeys) {
      if (row_delta) key_sel_ = (key_sel_ + kKeysRows + row_delta) % kKeysRows;
      if (direction || confirm) ChangeKeybind(direction);
    } else if (mode_ == OverlayMode::kSettings) {
      if (row_delta) {
        if (set_sel_ == DisplayModeOptIndex()) RevertDisplayModePreview();
        const int rows = HOST_VIDEO_OPTS_COUNT + kSettingsExtraRows;
        set_sel_ = (set_sel_ + rows + row_delta) % rows;
        set_armed_ = false;
      }
      if (direction || confirm) ChangeSetting(direction);
    }
  }

  // ---- scale ------------------------------------------------------------
  // ImGui space may differ from physical pixels (DPI). u = ImGui units per
  // physical output pixel, taken from the window the player actually sees.
  void ComputeUiScale(const ImGuiViewport* vp) {
    float phys_h = 0.0f;
    float dpi = 1.0f;
    if (window_) {
      phys_h = static_cast<float>(window_->GetActualPhysicalHeight());
      const float medium = static_cast<float>(window_->GetMediumDpi());
      if (medium > 0.0f) {
        dpi = static_cast<float>(window_->GetDpi()) / medium;
      }
    }
    const float u = phys_h > 1.0f ? vp->WorkSize.y / phys_h : 1.0f;
    min_rh_ = 22.0f * dpi * u;
  }

  void LogScale(const ImGuiViewport* vp, const ImGuiIO& io) {
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "scale display=%.0fx%.0f work=%.0fx%.0f fb_scale=%.2f phys=%ux%u dpi=%u "
                  "min_rh=%.1f resolution_scale=%s",
                  io.DisplaySize.x, io.DisplaySize.y, vp->WorkSize.x, vp->WorkSize.y,
                  io.DisplayFramebufferScale.x,
                  window_ ? window_->GetActualPhysicalWidth() : 0u,
                  window_ ? window_->GetActualPhysicalHeight() : 0u,
                  window_ ? window_->GetDpi() : 0u, min_rh_,
                  rex::cvar::GetFlagByName("resolution_scale").c_str());
    Note(buf);
  }

  static ImVec2 TextSz(float size, const char* t) {
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, t);
  }

  static float FitSize(float size, float max_w, const char* t) {
    const float w = TextSz(size, t).x;
    return (w > max_w && w > 0.0f) ? size * (max_w / w) : size;
  }

  void Note(const char* what) {
    REXLOG_INFO("host overlay: {} mode={}", what, static_cast<int>(mode_));
  }

  bool ConsumeHotkeyEdge(const char* which) {
#ifdef _WIN32
    // RegisterBind + PollHostHotkeys(GetAsyncKeyState) can both fire on one
    // physical F1/Esc; collapse to a single edge within the same press window.
    const ULONGLONG now = GetTickCount64();
    if (hotkey_edge_which_ && hotkey_edge_which_[0] &&
        std::strcmp(hotkey_edge_which_, which) == 0 &&
        now - hotkey_edge_ms_ < 80) {
      return false;
    }
    hotkey_edge_which_ = which;
    hotkey_edge_ms_ = now;
#else
    (void)which;
#endif
    return true;
  }

  void ToggleSettings(const char* hotkey = "F1") {
    if (!ConsumeHotkeyEdge(hotkey)) {
      return;
    }
    Note(hotkey);
    if (mode_ != OverlayMode::kNone) {
      // Hitz: F1 closes whichever overlay is open.
      CloseOverlay();
      return;
    }
    RefreshFromCvars();
    set_sel_ = 0;
    scroll_ = 0;
    set_armed_ = false;
    logged_scale_ = false;
    mode_ = OverlayMode::kSettings;
#ifdef _WIN32
    // Smoke/capture helper: empty flag file opens Redefine Keys immediately.
    char dir[MAX_PATH], flag[MAX_PATH];
    if (IniDir(dir, sizeof dir)) {
      std::snprintf(flag, sizeof flag, "%s\\open-keys.flag", dir);
      if (GetFileAttributesA(flag) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileA(flag);
        OpenKeys();
        Note("open-keys");
      }
    }
#endif
  }

  void ToggleQuit() {
    if (!ConsumeHotkeyEdge("ESC")) {
      return;
    }
    Note("ESC");
    if (mode_ == OverlayMode::kKeys) {
      if (key_wait_ >= 0) {
        // Esc while capturing: skip this input (Rebind all) or cancel.
        if (rebind_all_) {
          AdvanceRebind();
        } else {
          EndCapture();
        }
        return;
      }
      mode_ = OverlayMode::kSettings;
      return;
    }
    if (mode_ != OverlayMode::kNone) {
      CloseOverlay();
      return;
    }
    q_sel_ = 0;
    mode_ = OverlayMode::kQuit;
  }

  void CloseOverlay() {
    // Video changes staged while the menu was open take effect now.
    bool ask_to_restart = false;
    if (mode_ == OverlayMode::kSettings || mode_ == OverlayMode::kKeys) {
      for (int i = 0; i < HOST_VIDEO_OPTS_COUNT; ++i) {
        const auto& opt = kHostVideoOpts[i];
        if (sel_[i] != applied_sel_[i] &&
            nba_settings::VideoValueNeedsRestart(opt.cvar, opt.values[sel_[i]])) {
          ask_to_restart = true;
          break;
        }
      }
      CommitVideoChanges(/*live=*/true);
    }
    mode_ = ask_to_restart ? OverlayMode::kRestart : OverlayMode::kNone;
    q_sel_ = 0;
    set_armed_ = false;
    EndCapture();
  }

  void ConfirmDialogChoice() {
    if (q_sel_ == 0) {
      CloseOverlay();
      return;
    }
    if (!window_) return;
    if (mode_ == OverlayMode::kRestart && !StartNbaRestart()) {
      CloseOverlay();
      notice_ = "Restart failed. Your settings are saved for the next launch.";
#ifdef _WIN32
      notice_until_ms_ = GetTickCount64() + 9000;
#endif
      return;
    }
    nba_pause::Release();
    window_->RequestClose();
  }

  void RefreshFromCvars() {
    for (int i = 0; i < HOST_VIDEO_OPTS_COUNT; ++i) {
      const auto& opt = kHostVideoOpts[i];
      // Restart-only options show what the next start will use (settings.ini).
      const std::string cur = nba_settings::StoredVideoValue(opt.cvar);
      sel_[i] = HostVideoFindChoice(&opt, cur.empty() ? opt.values[opt.def] : cur.c_str());
    }
    applied_sel_ = sel_;
  }

  void RefreshKeybindsFromCvars() {
    for (int i = 0; i < HOST_KEYBIND_COUNT; ++i) {
      std::string cur = rex::cvar::GetFlagByName(kHostKeybinds[i].cvar);
      if (cur.empty()) {
        const std::string stored =
            IniGetSection(HOST_KEYBINDS_SECTION, kHostKeybinds[i].cvar, "");
        if (stored == HOST_KEYBIND_NONE) {
          cur.clear();  // deliberately cleared
        } else if (!stored.empty()) {
          cur = stored;
        } else {
          cur = kHostKeybinds[i].def;
        }
      }
      key_vals_[i] = cur;
    }
  }

  // ---- key mapper --------------------------------------------------------
  void OpenKeys() {
    RefreshKeybindsFromCvars();
    key_sel_ = 0;
    key_scroll_ = 0;
    EndCapture();
    mode_ = OverlayMode::kKeys;
  }

  void ApplyKeybind(int index, bool write_ini) {
    if (index < 0 || index >= HOST_KEYBIND_COUNT) {
      return;
    }
    const char* cvar = kHostKeybinds[index].cvar;
    const std::string& value = key_vals_[index];
    if (!rex::cvar::SetFlagByName(cvar, value)) {
      REXLOG_WARN("host overlay: rejected {}={}", cvar, value);
    }
    if (write_ini) {
      IniPutSection(HOST_KEYBINDS_SECTION, cvar,
                    value.empty() ? HOST_KEYBIND_NONE : value.c_str());
    }
  }

  // Slot picked: wait for every key to be released, then listen for ONE edge.
  void BeginCapture(int slot) {
    key_wait_ = slot;
    cap_state_ = CapState::kWaitRelease;
    cap_release_ms_ = 0;
    mod_pending_ = 0;
    std::memset(cap_prev_, 0, sizeof cap_prev_);
  }

  void EndCapture() {
    key_wait_ = -1;
    cap_state_ = CapState::kIdle;
    rebind_all_ = false;
    mod_pending_ = 0;
  }

  int CurrentInputIndex() const {
    return rebind_all_ ? rebind_idx_ : key_sel_ - kKeysTopRows;
  }

  void StartRebindAll() {
    rebind_all_ = true;
    rebind_idx_ = 0;
    key_sel_ = kKeysTopRows;
    BeginCapture(0);
    Note("rebind-all start");
  }

  // Move to the next input (after a capture, a skip or a clear).
  void AdvanceRebind() {
    if (!rebind_all_) {
      EndCapture();
      return;
    }
    ++rebind_idx_;
    if (rebind_idx_ >= HOST_KEYBIND_COUNT) {
      Note("rebind-all done");
      EndCapture();
      key_sel_ = 0;
      return;
    }
    key_sel_ = kKeysTopRows + rebind_idx_;
    BeginCapture(0);
  }

  void ClearCurrentInput() {
    const int idx = CurrentInputIndex();
    if (idx < 0 || idx >= HOST_KEYBIND_COUNT) {
      return;
    }
    char prim[64] = {}, alt[96] = {}, joined[192] = {};
    HostKeybindSplit(key_vals_[idx].c_str(), prim, sizeof prim, alt, sizeof alt);
    if (rebind_all_ || key_wait_ == 0) {
      prim[0] = 0;
    }
    if (rebind_all_ || key_wait_ == 1) {
      alt[0] = 0;
    }
    HostKeybindJoin(prim, alt, joined, sizeof joined);
    key_vals_[idx] = joined;
    ApplyKeybind(idx, /*write_ini=*/true);
    AdvanceRebind();
  }

  // Bind `name` to the current input. Rebind all: one key per input (primary),
  // alternate cleared. Single: only the picked slot changes.
  void AssignCapturedKey(const char* name) {
    const int idx = CurrentInputIndex();
    if (idx < 0 || idx >= HOST_KEYBIND_COUNT) {
      EndCapture();
      return;
    }
    // One key drives one action: strip it from every other input.
    for (int i = 0; i < HOST_KEYBIND_COUNT; ++i) {
      if (i == idx) {
        continue;
      }
      char p[64] = {}, a[96] = {}, j[192] = {};
      HostKeybindSplit(key_vals_[i].c_str(), p, sizeof p, a, sizeof a);
      int dirty = 0;
      if (EqualsIgnoreCase(p, name)) {
        p[0] = 0;
        dirty = 1;
      }
      if (EqualsIgnoreCase(a, name)) {
        a[0] = 0;
        dirty = 1;
      }
      if (dirty) {
        HostKeybindJoin(p, a, j, sizeof j);
        key_vals_[i] = j;
        ApplyKeybind(i, /*write_ini=*/true);
      }
    }
    char prim[64] = {}, alt[96] = {}, joined[192] = {};
    HostKeybindSplit(key_vals_[idx].c_str(), prim, sizeof prim, alt, sizeof alt);
    if (rebind_all_) {
      std::snprintf(prim, sizeof prim, "%s", name);
      alt[0] = 0;
    } else if (key_wait_ == 0) {
      std::snprintf(prim, sizeof prim, "%s", name);
    } else {
      std::snprintf(alt, sizeof alt, "%s", name);
    }
    HostKeybindJoin(prim, alt, joined, sizeof joined);
    key_vals_[idx] = joined;
    ApplyKeybind(idx, /*write_ini=*/true);
    char msg[160];
    std::snprintf(msg, sizeof msg, "bound %s <- %s", kHostKeybinds[idx].cvar, name);
    Note(msg);
    AdvanceRebind();
  }

#ifdef _WIN32
  static bool VkCapturable(int vk) {
    if (vk == VK_ESCAPE || vk == VK_BACK) return false;  // handled separately
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) return false;  // generic duplicates
    if (vk == VK_LMENU || vk == VK_RMENU) return false;  // Alt reserved
    if (HostKeybindReservedVk(vk)) return false;
    if (vk >= 0x08 && vk <= 0x5A) return true;   // keys, digits, letters
    if (vk >= 0x60 && vk <= 0x87) return true;   // numpad + F-keys
    if (vk >= 0xBA && vk <= 0xC0) return true;   // OEM punctuation
    if (vk >= 0xDB && vk <= 0xDE) return true;
    if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL) return true;
    return false;
  }

  static bool IsShiftVk(int vk) { return vk == VK_LSHIFT || vk == VK_RSHIFT; }
  static bool IsCtrlVk(int vk) { return vk == VK_LCONTROL || vk == VK_RCONTROL; }

  // Edge-triggered capture. After a slot is picked we wait until ALL keys are
  // up (so the key that activated the slot, or any held/auto-repeating key,
  // can never be captured), then take exactly one key-down edge. Modifiers are
  // resolved on release so "Shift+Up" style binds still work.
  void CaptureStep(bool ours) {
    bool cur[256];
    bool any_down = false;
    for (int vk = 8; vk < 256; ++vk) {
      const bool d = (GetAsyncKeyState(vk) & 0x8000) != 0;
      cur[vk] = d;
      if (d && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU) {
        any_down = true;
      }
    }
    const ULONGLONG now = GetTickCount64();
    if (cap_state_ == CapState::kWaitRelease) {
      if (any_down) {
        cap_release_ms_ = 0;
      } else if (cap_release_ms_ == 0) {
        cap_release_ms_ = now;
      } else if (now - cap_release_ms_ >= 150) {
        cap_state_ = CapState::kArmed;
        mod_pending_ = 0;
      }
      std::memcpy(cap_prev_, cur, sizeof cap_prev_);
      return;
    }
    if (cap_state_ != CapState::kArmed || !ours) {
      std::memcpy(cap_prev_, cur, sizeof cap_prev_);
      return;
    }
    // Backspace clears the current input.
    if (cur[VK_BACK] && !cap_prev_[VK_BACK]) {
      std::memcpy(cap_prev_, cur, sizeof cap_prev_);
      ClearCurrentInput();
      return;
    }
    const bool shift_held = cur[VK_LSHIFT] || cur[VK_RSHIFT];
    int captured = 0;
    for (int vk = 8; vk < 256 && !captured; ++vk) {
      if (!cur[vk] || cap_prev_[vk] || !VkCapturable(vk)) {
        continue;  // only fresh key-down edges count
      }
      if (IsShiftVk(vk) || IsCtrlVk(vk)) {
        mod_pending_ = IsShiftVk(vk) ? VK_SHIFT : VK_CONTROL;
        continue;  // decide on release / combo
      }
      captured = vk;
    }
    if (captured) {
      char name[64] = {};
      if (HostVkToRexName(captured, shift_held ? 1 : 0, name, sizeof name) && name[0]) {
        std::memcpy(cap_prev_, cur, sizeof cap_prev_);
        AssignCapturedKey(name);
        return;
      }
    } else if (mod_pending_) {
      const bool still = mod_pending_ == VK_SHIFT ? (cur[VK_LSHIFT] || cur[VK_RSHIFT])
                                                  : (cur[VK_LCONTROL] || cur[VK_RCONTROL]);
      if (!still) {
        const char* name = mod_pending_ == VK_SHIFT ? "Shift" : "Ctrl";
        mod_pending_ = 0;
        std::memcpy(cap_prev_, cur, sizeof cap_prev_);
        AssignCapturedKey(name);
        return;
      }
    }
    std::memcpy(cap_prev_, cur, sizeof cap_prev_);
  }
#else
  void CaptureStep(bool) {}
#endif

  void ChangeKeybind(int dir) {
    if (key_sel_ == 0) {
      if (dir == 0) {
        StartRebindAll();
      }
      return;
    }
    if (key_sel_ >= kKeysBackRow) {
      if (dir == 0) {
        mode_ = OverlayMode::kSettings;
        EndCapture();
      }
      return;
    }
    // Individual rebinding: Enter/Space = primary, Left = primary, Right = alternate.
    rebind_all_ = false;
    BeginCapture(dir > 0 ? 1 : 0);
  }

  // ---- video settings -----------------------------------------------------
  // Video options are only staged (sel_) while F1 is open. On close, every
  // option that changed since the menu opened is saved to settings.ini right
  // away and applied to the running game on the UI thread (outside ImGui paint).
  // `live` false (Quit from F1) only saves.
  bool BindBlocked() const {
#ifdef _WIN32
    return GetTickCount64() < bind_block_until_ms_;
#else
    return false;
#endif
  }

  void CommitVideoChanges(bool live) {
    struct Change {
      std::string key, value, label;
    };
    std::vector<Change> changes;
    for (int i = 0; i < HOST_VIDEO_OPTS_COUNT; ++i) {
      if (sel_[i] == applied_sel_[i]) {
        continue;
      }
      const auto& opt = kHostVideoOpts[i];
      changes.push_back({opt.cvar, opt.values[sel_[i]], opt.label});
      IniPutVideo(opt.cvar, opt.values[sel_[i]]);
    }
    applied_sel_ = sel_;
    if (changes.empty() || !live) {
      return;
    }
    auto apply = [this, changes] {
      std::string no_effect;
      for (const auto& c : changes) {
        const auto r = nba_settings::ApplyVideoSettingRuntime(c.key.c_str(), c.value.c_str());
        char msg[160];
        std::snprintf(msg, sizeof msg, "apply %s=%s -> %s", c.key.c_str(), c.value.c_str(),
                      r == nba_settings::VideoApply::kLive
                          ? "live"
                          : (r == nba_settings::VideoApply::kRestart ? "restart required"
                                                                     : "no effect in this build"));
        Note(msg);
        REXLOG_INFO("settings: {}", msg);
        if (r == nba_settings::VideoApply::kNoEffect) {
          no_effect += (no_effect.empty() ? "" : ", ") + c.label;
        }
      }
      if (!std::strcmp(changes.back().key.c_str(), HOST_RESOLUTION_SCALE_KEY)) {
        logged_scale_ = false;
      }
      BuildNotice(no_effect);
      // A fullscreen/exclusive switch can stall this thread for a second or two,
      // during which the F1/Esc press that closed the menu is still queued as an
      // SDK keybind event. It is delivered right after this returns; drop it so it
      // cannot reopen the menu (polling already handled that physical press).
#ifdef _WIN32
      bind_block_until_ms_ = GetTickCount64() + 400;
#endif
    };
    if (window_) {
      window_->app_context().CallInUIThreadDeferred(apply);
    } else {
      apply();
    }
  }

  // "Restart required for: X, Y" for every option whose saved value differs
  // from what the running GPU was created with (and options this build ignores).
  std::string RestartList() const {
    std::string list;
    for (int i = 0; i < HOST_VIDEO_OPTS_COUNT; ++i) {
      if (nba_settings::VideoValueNeedsRestart(kHostVideoOpts[i].cvar,
                                               kHostVideoOpts[i].values[sel_[i]])) {
        list += (list.empty() ? "" : ", ") + std::string(kHostVideoOpts[i].label);
      }
    }
    return list;
  }

  void BuildNotice(const std::string& no_effect) {
    std::string text;
    const std::string restart = RestartList();
    if (!restart.empty()) {
      text = "Restart required for: " + restart;
    }
    if (!no_effect.empty()) {
      text += std::string(text.empty() ? "" : "   |   ") + no_effect + ": not supported by this build";
    }
    notice_ = text;
#ifdef _WIN32
    notice_until_ms_ = GetTickCount64() + 9000;
#endif
    if (!text.empty()) {
      REXLOG_INFO("settings: {}", text);
    }
  }

  void DrawNotice(const ImGuiIO& io) {
#ifdef _WIN32
    if (notice_.empty() || GetTickCount64() > notice_until_ms_) {
      return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float size = (std::max)(14.0f, io.DisplaySize.y * 0.026f);
    const ImVec2 ts = TextSz(size, notice_.c_str());
    const float pad = size * 0.5f;
    const float x = (io.DisplaySize.x - ts.x) * 0.5f;
    const float y = io.DisplaySize.y * 0.90f;
    dl->AddRectFilled(ImVec2(x - pad, y - pad * 0.6f), ImVec2(x + ts.x + pad, y + size + pad * 0.6f),
                      IM_COL32(14, 22, 40, 235), 4.0f);
    dl->AddRect(ImVec2(x - pad, y - pad * 0.6f), ImVec2(x + ts.x + pad, y + size + pad * 0.6f), kAccent,
                4.0f, 0, 2.0f);
    dl->AddText(nullptr, size, ImVec2(x, y), IM_COL32(255, 255, 255, 255), notice_.c_str());
#else
    (void)io;
#endif
  }
  int DisplayModeOptIndex() const {
    for (int i = 0; i < HOST_VIDEO_OPTS_COUNT; ++i) {
      if (HostDisplayModeIs(kHostVideoOpts[i].cvar)) {
        return i;
      }
    }
    return -1;
  }

  int LiveDisplayModeSel() const {
    const int idx = DisplayModeOptIndex();
    if (idx < 0) {
      return 0;
    }
    const auto& opt = kHostVideoOpts[idx];
    const std::string cur = nba_settings::CurrentVideoValue(opt.cvar);
    return HostVideoFindChoice(&opt, cur.c_str());
  }

  // Display mode is staged like every other option now (applied on close), so
  // there is no separate preview to undo when the cursor moves.
  void RevertDisplayModePreview() {}

  void ChangeSetting(int dir) {
    if (set_sel_ >= HOST_VIDEO_OPTS_COUNT) {
      const int extra = set_sel_ - HOST_VIDEO_OPTS_COUNT;
      if (dir == 0) {
        if (extra == kRowRedefineKeys) {
          OpenKeys();
        } else if (extra == kRowClose) {
          CloseOverlay();
        } else if (extra == kRowQuit) {
          if (set_armed_) {
            CommitVideoChanges(/*live=*/false);  // keep staged choices for next start
            nba_pause::Release();
            if (window_) {
              window_->RequestClose();
            }
          } else {
            set_armed_ = true;
          }
        }
      }
      return;
    }
    const auto& opt = kHostVideoOpts[set_sel_];
    if (opt.count <= 0) {
      return;
    }
    // Staged only: everything is applied (and saved) when the menu closes.
    if (dir == 0) {
      dir = 1;
    }
    sel_[set_sel_] = (sel_[set_sel_] + dir + opt.count) % opt.count;
    set_armed_ = false;
  }

  // ---- drawing -------------------------------------------------------------
  void DrawButton(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* label, bool on,
                  float font_size) {
    dl->AddRectFilled(a, b, on ? kAccent : kRowOff);
    const ImVec2 ts = TextSz(font_size, label);
    dl->AddText(nullptr, font_size,
                ImVec2(a.x + (b.x - a.x - ts.x) * 0.5f, a.y + (b.y - a.y - ts.y) * 0.5f),
                on ? kTxtSel : kTxt, label);
  }

  // Panel metrics shared by Settings and Keys.
  struct Panel {
    float dx, dy, dw, dh, rh, border, pad, head_band, hint_band;
    int visible;
  };

  Panel MakePanel(const ImGuiViewport* vp, int rows) const {
    Panel p{};
    const float cw = vp->WorkSize.x;
    const float ch = vp->WorkSize.y;
    float rh = ch * 0.045f;
    if (rh < min_rh_) {
      rh = min_rh_;
    }
    float dw = rh * 22.0f;
    if (dw > cw * 0.95f) {
      dw = cw * 0.95f;
      rh = dw / 22.0f;
    }
    p.rh = rh;
    p.dw = dw;
    p.visible = std::min(rows, 14);
    p.border = (std::max)(6.0f, rh * 0.22f + 4.0f);
    p.pad = (std::max)(8.0f, rh * 0.28f);
    p.head_band = rh * 1.15f;
    p.hint_band = rh * 1.55f;
    p.dh = p.border * 2.0f + p.head_band + p.visible * rh + p.hint_band;
    p.dx = vp->WorkPos.x + (cw - dw) * 0.5f;
    p.dy = vp->WorkPos.y + (ch - p.dh) * 0.5f;
    return p;
  }

  void DrawHeading(ImDrawList* dl, const Panel& p, ImVec2 panel0, ImVec2 panel1,
                   const char* heading) {
    float head_size = p.rh * 0.72f;
    head_size = FitSize(head_size, (panel1.x - panel0.x) - p.pad * 2.0f, heading);
    const ImVec2 hs = TextSz(head_size, heading);
    const float hx = panel0.x + ((panel1.x - panel0.x) - hs.x) * 0.5f;
    const float hy = panel0.y + (p.head_band - head_size) * 0.5f;
    dl->AddText(nullptr, head_size, ImVec2(hx, hy), IM_COL32(255, 255, 255, 255), heading);
  }

  // One row: label left, value right, both sized from the row height.
  void DrawRow(ImDrawList* dl, const Panel& p, ImVec2 r0, ImVec2 r1, const char* label,
               const char* right, bool sel) {
    dl->AddRectFilled(r0, r1, sel ? kAccent : kRowOff);
    float font = p.rh * 0.56f;
    const float text_pad = p.pad * 0.6f;
    const float avail = (r1.x - r0.x) - text_pad * 2.0f;
    const float lw = TextSz(font, label).x;
    const float rw = right && right[0] ? TextSz(font, right).x : 0.0f;
    const float gap = font * 1.2f;
    if (lw + rw + gap > avail && lw + rw > 0.0f) {
      font *= avail / (lw + rw + gap);
    }
    const float text_y = r0.y + ((r1.y - r0.y) - font) * 0.5f;
    dl->AddText(nullptr, font, ImVec2(r0.x + text_pad, text_y), sel ? kTxtSel : kTxt, label);
    if (right && right[0]) {
      const ImVec2 ts = TextSz(font, right);
      dl->AddText(nullptr, font, ImVec2(r1.x - text_pad - ts.x, text_y), sel ? kTxtSel : kTxt,
                  right);
    }
  }

  void DrawHint(ImDrawList* dl, const Panel& p, float row_left, float row_right,
                float rows_top, const char* hint, ImU32 col) {
    float size = p.rh * 0.40f;
    size = FitSize(size, row_right - row_left, hint);
    dl->AddText(nullptr, size, ImVec2(row_left, rows_top + p.visible * p.rh + p.rh * 0.25f),
                col, hint);
  }

  void DrawQuit(ImDrawList* dl, const ImGuiViewport* vp) {
    const float cw = vp->WorkSize.x;
    const float ch = vp->WorkSize.y;
    float dw = ch * 4.0f / 5.0f;
    if (dw > cw * 0.9f) {
      dw = cw * 0.9f;
    }
    const float dh = dw * 9.0f / 20.0f;
    const float dx = vp->WorkPos.x + (cw - dw) * 0.5f;
    const float dy = vp->WorkPos.y + (ch - dh) * 0.5f;

    dl->AddRectFilled(ImVec2(dx, dy), ImVec2(dx + dw, dy + dh), kAccent);
    const float inset = dh / 40.0f + 2.0f;
    dl->AddRectFilled(ImVec2(dx + inset, dy + inset),
                      ImVec2(dx + dw - inset, dy + dh - inset), kPanel);

    const bool restart = mode_ == OverlayMode::kRestart;
    const char* title = restart ? "Restart to apply these settings?" : "Do you want to quit?";
    const float title_size = FitSize(dh * 0.17f, dw - inset * 4.0f, title);
    const ImVec2 ts = TextSz(title_size, title);
    dl->AddText(nullptr, title_size, ImVec2(dx + (dw - ts.x) * 0.5f, dy + dh * 0.22f),
                IM_COL32(255, 255, 255, 255), title);

    const float bw = dw * 0.40f;
    const float bh = dh * 0.28f;
    const float by = dy + dh * 0.58f;
    const ImVec2 no0(dx + dw * 0.5f - bw - dw * 0.04f, by);
    const ImVec2 no1(no0.x + bw, by + bh);
    const ImVec2 yes0(dx + dw * 0.5f + dw * 0.04f, by);
    const ImVec2 yes1(yes0.x + bw, by + bh);
    const char* no = restart ? "Continue playing" : "NO";
    const char* yes = restart ? "Restart now" : "YES";
    const float btn_font = FitSize(bh * 0.46f, bw * 0.86f, no);
    DrawButton(dl, no0, no1, no, q_sel_ == 0, btn_font);
    DrawButton(dl, yes0, yes1, yes, q_sel_ == 1, btn_font);
    if (restart) {
      const char* note = "Restarting ends the current match.";
      const float note_size = FitSize(dh * 0.09f, dw - inset * 4.0f, note);
      const auto size = TextSz(note_size, note);
      dl->AddText(nullptr, note_size, ImVec2(dx + (dw - size.x) * 0.5f, dy + dh * 0.42f), kHint, note);
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (ImGui::IsMouseHoveringRect(no0, no1)) { q_sel_ = 0; ConfirmDialogChoice(); }
        else if (ImGui::IsMouseHoveringRect(yes0, yes1)) { q_sel_ = 1; ConfirmDialogChoice(); }
      }
    }
  }

  void DrawSettings(ImDrawList* dl, const ImGuiViewport* vp) {
    const int rows = HOST_VIDEO_OPTS_COUNT + kSettingsExtraRows;
    const Panel p = MakePanel(vp, rows);
    dl->AddRectFilled(ImVec2(p.dx, p.dy), ImVec2(p.dx + p.dw, p.dy + p.dh), kAccent);
    const ImVec2 panel0(p.dx + p.border, p.dy + p.border);
    const ImVec2 panel1(p.dx + p.dw - p.border, p.dy + p.dh - p.border);
    dl->AddRectFilled(panel0, panel1, kPanel);
    dl->PushClipRect(panel0, panel1, true);

    DrawHeading(dl, p, panel0, panel1, "Settings");

    if (set_sel_ < scroll_) {
      scroll_ = set_sel_;
    }
    if (set_sel_ >= scroll_ + p.visible) {
      scroll_ = set_sel_ - p.visible + 1;
    }
    if (scroll_ < 0) {
      scroll_ = 0;
    }

    const float row_left = panel0.x + p.pad;
    const float row_right = panel1.x - p.pad;
    const float rows_top = panel0.y + p.head_band;
    for (int vis = 0; vis < p.visible; ++vis) {
      const int i = scroll_ + vis;
      if (i >= rows) {
        break;
      }
      const bool sel = (i == set_sel_);
      const float y = rows_top + vis * p.rh;
      const ImVec2 r0(row_left, y + p.rh * 0.08f);
      const ImVec2 r1(row_right, y + p.rh * 0.92f);

      char right[96] = {};
      const char* label = nullptr;
      if (i < HOST_VIDEO_OPTS_COUNT) {
        label = kHostVideoOpts[i].label;
        const int s = sel_[static_cast<size_t>(i)];
        if (nba_settings::VideoValueNeedsRestart(kHostVideoOpts[i].cvar, kHostVideoOpts[i].values[s])) {
          std::snprintf(right, sizeof right, "<  %s  >  (restart)", kHostVideoOpts[i].texts[s]);
        } else {
          std::snprintf(right, sizeof right, "<  %s  >", kHostVideoOpts[i].texts[s]);
        }
      } else if (i == HOST_VIDEO_OPTS_COUNT + kRowRedefineKeys) {
        label = "Redefine Keys";
        std::snprintf(right, sizeof right, ">");
      } else if (i == HOST_VIDEO_OPTS_COUNT + kRowClose) {
        label = "Close menu";
      } else {
        label = "Quit game";
        if (set_armed_) {
          std::snprintf(right, sizeof right, "press again to quit");
        }
      }
      DrawRow(dl, p, r0, r1, label, right, sel);
    }

    DrawHint(dl, p, row_left, row_right, rows_top,
             "Up/Down choose, Left/Right change. Changes apply when you close this menu (F1, Esc or B).",
             kHint);
    dl->PopClipRect();
  }

  void HandleQuitKeys() {
    // Keyboard navigation is handled by OnKeyDown.
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_DPAD_LEFT)) {
      q_sel_ = 0;
    }
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT)) {
      q_sel_ = 1;
    }
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_A)) ConfirmDialogChoice();
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_B)) {
      CloseOverlay();
    }
  }

  void HandleSettingsKeys() {
    // Keyboard navigation is handled by OnKeyDown.
    const int rows = HOST_VIDEO_OPTS_COUNT + kSettingsExtraRows;
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_DPAD_UP)) {
      if (set_sel_ == DisplayModeOptIndex()) {
        RevertDisplayModePreview();
      }
      set_sel_ = (set_sel_ + rows - 1) % rows;
      set_armed_ = false;
    }
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_DPAD_DOWN)) {
      if (set_sel_ == DisplayModeOptIndex()) {
        RevertDisplayModePreview();
      }
      set_sel_ = (set_sel_ + 1) % rows;
      set_armed_ = false;
    }
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_DPAD_LEFT)) {
      ChangeSetting(-1);
    }
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT)) {
      ChangeSetting(+1);
    }
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_A)) {
      ChangeSetting(0);
    }
    // B closes (and applies) like the hint says.
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_B)) {
      CloseOverlay();
    }
  }

  void DrawKeys(ImDrawList* dl, const ImGuiViewport* vp) {
    const Panel p = MakePanel(vp, kKeysRows);
    dl->AddRectFilled(ImVec2(p.dx, p.dy), ImVec2(p.dx + p.dw, p.dy + p.dh), kAccent);
    const ImVec2 panel0(p.dx + p.border, p.dy + p.border);
    const ImVec2 panel1(p.dx + p.dw - p.border, p.dy + p.dh - p.border);
    dl->AddRectFilled(panel0, panel1, kPanel);
    dl->PushClipRect(panel0, panel1, true);

    DrawHeading(dl, p, panel0, panel1, "Redefine Keys");

    if (key_sel_ < key_scroll_) {
      key_scroll_ = key_sel_;
    }
    if (key_sel_ >= key_scroll_ + p.visible) {
      key_scroll_ = key_sel_ - p.visible + 1;
    }
    if (key_scroll_ < 0) {
      key_scroll_ = 0;
    }

    const float row_left = panel0.x + p.pad;
    const float row_right = panel1.x - p.pad;
    const float rows_top = panel0.y + p.head_band;
    for (int vis = 0; vis < p.visible; ++vis) {
      const int i = key_scroll_ + vis;
      if (i >= kKeysRows) {
        break;
      }
      const bool sel = (i == key_sel_);
      const float y = rows_top + vis * p.rh;
      const ImVec2 r0(row_left, y + p.rh * 0.08f);
      const ImVec2 r1(row_right, y + p.rh * 0.92f);

      char right[120] = {};
      const char* label = nullptr;
      if (i == 0) {
        label = "Rebind all keys";
        std::snprintf(right, sizeof right, rebind_all_ ? "in progress" : ">");
      } else if (i < kKeysBackRow) {
        const int idx = i - kKeysTopRows;
        label = kHostKeybinds[idx].label;
        char prim[64] = {}, alt[96] = {}, pd[64] = {}, ad[96] = {};
        HostKeybindSplit(key_vals_[idx].c_str(), prim, sizeof prim, alt, sizeof alt);
        HostKeybindDisplayName(prim, pd, sizeof pd);
        HostKeybindDisplayName(alt, ad, sizeof ad);
        const bool waiting_here = key_wait_ >= 0 && CurrentInputIndex() == idx;
        if (waiting_here) {
          const char* what = rebind_all_ ? "key" : (key_wait_ == 0 ? "primary key" : "alternate key");
          if (cap_state_ == CapState::kWaitRelease) {
            std::snprintf(right, sizeof right, "release all keys...");
          } else {
            std::snprintf(right, sizeof right, "press %s...", what);
          }
        } else {
          std::snprintf(right, sizeof right, "<  %s / %s  >", pd, ad);
        }
      } else {
        label = "Back";
      }
      DrawRow(dl, p, r0, r1, label, right, sel);
    }

    char hint[220];
    if (key_wait_ >= 0) {
      const int idx = CurrentInputIndex();
      const char* name = (idx >= 0 && idx < HOST_KEYBIND_COUNT) ? kHostKeybinds[idx].label : "";
      if (rebind_all_) {
        std::snprintf(hint, sizeof hint,
                      "Waiting for: %s (%d of %d).  Esc = skip, Backspace = clear.", name,
                      idx + 1, HOST_KEYBIND_COUNT);
      } else {
        std::snprintf(hint, sizeof hint,
                      "Waiting for: %s (%s).  Esc = cancel, Backspace = clear.", name,
                      key_wait_ == 0 ? "primary" : "alternate");
      }
    } else {
      std::snprintf(hint, sizeof hint,
                    "Enter/Left = primary, Right = alternate, then press a key. Esc returns.");
    }
    DrawHint(dl, p, row_left, row_right, rows_top, hint, kHint);
    dl->PopClipRect();
  }

  void HandleKeysKeys() {
    if (key_wait_ >= 0) {
      if (PadPressed(rex::input::X_INPUT_GAMEPAD_B)) {
        rebind_all_ = false;
        EndCapture();
      }
      return;  // keyboard capture is handled in CaptureStep
    }
    // Keyboard navigation is handled by OnKeyDown.
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_DPAD_UP)) {
      key_sel_ = (key_sel_ + kKeysRows - 1) % kKeysRows;
    }
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_DPAD_DOWN)) {
      key_sel_ = (key_sel_ + 1) % kKeysRows;
    }
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_DPAD_LEFT)) {
      ChangeKeybind(-1);
    }
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT)) {
      ChangeKeybind(+1);
    }
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_A)) {
      ChangeKeybind(0);
    }
    if (PadPressed(rex::input::X_INPUT_GAMEPAD_B)) {
      mode_ = OverlayMode::kSettings;
      EndCapture();
    }
  }

  rex::ui::Window* window_ = nullptr;
  OverlayMode mode_ = OverlayMode::kNone;
  int q_sel_ = 0;
  int set_sel_ = 0;
  int scroll_ = 0;
  int key_sel_ = 0;
  int key_scroll_ = 0;
  int key_wait_ = -1; /* -1 idle, 0 primary, 1 alternate */
  bool rebind_all_ = false;
  int rebind_idx_ = 0;
  CapState cap_state_ = CapState::kIdle;
  unsigned long long cap_release_ms_ = 0;
  int mod_pending_ = 0;
  bool cap_prev_[256] = {};
  float min_rh_ = 22.0f;
  bool logged_scale_ = false;
  bool set_armed_ = false;
  std::array<uint16_t, rex::input::kMaxGuestUsers> pad_buttons_{};
  uint16_t pad_pressed_ = 0;
  bool pad_input_blocked_ = false;
  bool f1_down_ = false;
  bool esc_down_ = false;
  const char* hotkey_edge_which_ = nullptr;
  unsigned long long hotkey_edge_ms_ = 0;
  std::vector<int> sel_;
  std::vector<int> applied_sel_;  // sel_ as of menu open / last commit
  std::string notice_;
  unsigned long long notice_until_ms_ = 0;
  unsigned long long bind_block_until_ms_ = 0;
  std::vector<std::string> key_vals_;
};

class NbaHostOverlayImpl final : public NbaHostOverlay {
 public:
  NbaHostOverlayImpl(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window)
      : dialog_(std::make_unique<HostOverlayDialog>(drawer, window)) {
    drawer->AddDialog(dialog_.get());
  }

  void ApplyIniFile() override { dialog_->ApplyIniFile(); }

 private:
  std::unique_ptr<HostOverlayDialog> dialog_;
};

}  // namespace

#ifdef _WIN32
REX_HOOK_RAW(__imp__XamInputGetState) {
  static auto* target = GetSdkInputImport("__imp__XamInputGetState");
  CallSdkInputImport(target, ctx, base);
}

REX_HOOK_RAW(__imp__XamInputGetKeystrokeEx) {
  static auto* target = GetSdkInputImport("__imp__XamInputGetKeystrokeEx");
  CallSdkInputImport(target, ctx, base);
}
#endif

std::unique_ptr<NbaHostOverlay> CreateNbaHostOverlay(rex::ui::ImGuiDrawer* drawer,
                                                     rex::ui::Window* window) {
  return std::make_unique<NbaHostOverlayImpl>(drawer, window);
}

void NbaApplyHostSettingsIni(NbaHostOverlay* overlay) {
  if (overlay) {
    overlay->ApplyIniFile();
  } else {
    nba_settings::ApplyIni();
  }
}
