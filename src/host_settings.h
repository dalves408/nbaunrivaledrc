// Built-in launch defaults + settings.ini application for NBA Unrivaled.
//
// Precedence for every cvar (highest first):
//   1. explicit command-line flag
//   2. %LOCALAPPDATA%\NBAUnrivaled\settings.ini ([display] + [keybinds])
//   3. built-in defaults (what the old long shortcut passed, but 60 fps)
// so a bare double-click of nbaunrivaled.exe behaves like the old shortcut,
// config.exe / F1 settings apply without any launcher, and flags still win.
#pragma once

#include <filesystem>
#include <string>

namespace rex {
struct PathConfig;
}

namespace nba_settings {

// The Launcher manifest points at this same per-user directory.
std::filesystem::path LogDir();

// Called from OnConfigurePaths (after the SDK parsed the command line, before
// the runtime exists). Records which flags were given on the command line,
// resolves the game/user/cache/metadata roots, shows the "game files missing"
// box and exits when <exe dir>\game\default.xex is absent (and no explicit
// --game_data_root was given), applies built-in defaults, then settings.ini.
void ApplyStartup(rex::PathConfig& paths);

// Retry settings whose cvars were not registered yet (GPU plugin cvars). Cheap;
// called from several app hooks.
void FlushPending();

// Re-read settings.ini and apply it (video + keybinds). Safe to call again.
void ApplyIni();

// Apply one visible video choice (ini key + stored value) to the live cvars.
// Handles the virtual keys (display_mode, frame_rate, anti_aliasing) and the
// automatic resolution-scale fixes.
void ApplyVideoSetting(const char* key, const char* value);

// Current stored-value for a visible video key, derived from the live cvars.
std::string CurrentVideoValue(const char* key);

// Write the messages collected before logging was ready (call once logging is
// up) and a one-line effective-settings summary.
void FlushStartupLog();
void ReportMissingGameFiles();

// ---- Live apply (F1 close) ------------------------------------------------

enum class VideoApply {
  kLive,        // took effect immediately
  kRestart,     // saved, but the GPU only reads it at startup
  kNoEffect,    // saved, but this SDK build cannot honour it at all
};

// Remember the GPU init-only values the running GPU plugin was created with
// (resolution scale, native MSAA). Call once after the runtime is set up.
void CaptureBootGpuState();

// Apply one visible video choice to the running game. Safe to call from the UI
// thread outside ImGui paint. Never changes a cvar the GPU already consumed.
VideoApply ApplyVideoSettingRuntime(const char* key, const char* value);

// True when `value` for `key` differs from what the running GPU was created
// with and so needs a restart. False for every live option.
bool VideoValueNeedsRestart(const char* key, const char* value);

// Value F1 should show for `key`: settings.ini (what the next start will use)
// for options that need a restart, otherwise the live cvar state.
std::string StoredVideoValue(const char* key);

}  // namespace nba_settings
