// nbaunrivaled - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>

#include <rex/filesystem.h>
#include <rex/rex_app.h>
#include <rex/perf/counter.h>

#include <rex/input/input_system.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>

#include "host_overlay.h"
#include "host_pause.h"
#include "host_restart.h"
#include "host_settings.h"
#include "host_timer_resolution.h"
#include "frame_pacer.h"
#include "perf_hud.h"
#include "langoverride_log_filter.h"
#include "nba_build_info.h"

class NbaUnrivaledApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    WaitForNbaRestartParent();
    return std::unique_ptr<NbaUnrivaledApp>(new NbaUnrivaledApp(ctx, "nbaunrivaled",
        PPCImageConfig));
  }

  // Double-clicking nbaunrivaled.exe with no arguments must behave like the old
  // long shortcut (60 fps instead of 120): game folder = <exe dir>\game, saves /
  // cache / metadata under %LOCALAPPDATA%\NBAUnrivaled, GPU plugin, license
  // unlock, keyboard+mouse mode, ... Built-in defaults, then settings.ini
  // (config.exe / F1), are applied only where the command line did not already
  // set the flag, so explicit flags still win. See host_settings.cpp. Shows a
  // "game files missing" box and exits when <exe dir>\game\default.xex is absent.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    nba_settings::ApplyStartup(paths);
  }

  // Retry settings for cvars the GPU plugin registers late (no-op once applied).
  void OnPreSetup(rex::RuntimeConfig& config) override {
    (void)config;
    nba_settings::FlushPending();
  }
  void OnPostSetup() override {
    nba_settings::FlushPending();
    // The GPU plugin has consumed its init-only flags by now (resolution scale,
    // MSAA, ...). Remember them so F1 can tell which edits need a restart.
    nba_settings::CaptureBootGpuState();
    REXLOG_INFO("Startup: runtime initialized (headless={}, renderer={}, input={})",
                rex::cvar::GetFlagByName("headless"), rex::cvar::GetFlagByName("gpu_plugin"),
                rex::cvar::GetFlagByName("input_backend"));
    // The SDK gates ALL guest input on !ImGui WantCaptureMouse whenever any of
    // its own overlays exists. Same rule here (so the SDK console / F4 / F7
    // windows still behave), but F3 no longer creates an SDK window and the
    // perf HUD draws without ImGui windows, so nothing of ours can wedge input.
    if (auto* input_sys = static_cast<rex::input::InputSystem*>(runtime()->input_system())) {
      input_sys->SetActiveCallback([this]() {
        if (window() && !window()->HasFocus()) return false;
        if (nba_pause::IsPaused()) return true;  // our F1/Esc menu is up
        if (!imgui_drawer()) return true;
        return !imgui_drawer()->GetIO().WantCaptureMouse;
      });
    }
  }

  // Never leave the guest stalled while the window is closing.
  bool OnWindowCloseRequested() override {
    nba_pause::Release();
    return true;
  }

  void OnConfigureLogging(rex::LogConfig& config) override {
    config.log_dir = nba_settings::LogDir();
    config.dir_budget_bytes = 32 * 1024 * 1024;
    // A short token stays with each error even if a report contains only the log tail.
    config.file_pattern = std::string("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [t%t] [b:") +
                          nba_build::kToken + "/" + nba_build::kCpuVariant + "] %v";
    // Optional-file probes can produce many warnings in the front end. Keep
    // those diagnostics, but avoid flushing the file on every failed probe.
    config.flush_level = spdlog::level::err;
    config.flush_interval = std::chrono::seconds{1};
  }

  void OnPostLoadXexImage() override {
    REXLOG_INFO("Startup: loaded game executable (title=584109D4, media=634F9F4D)");
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    // Hitz-style ESC quit + F1 video settings (ReXGlue/Xenos lever set).
    nba_settings::FlushPending();
    // F3 is the perf HUD's key. The SDK also binds F3 to its own green "Debug"
    // ImGui window (an empty extra window that also gated game input); drop it.
    rex::ui::UnregisterBind("bind_debug_overlay");
    host_overlay_ = CreateNbaHostOverlay(drawer, window());
    NbaApplyHostSettingsIni(host_overlay_.get());
    // Test-build FPS/CPU/GPU HUD (--nba_perf_hud=true or F3); idle when off.
    CreatePerfHud(drawer, window());
  }

  void OnPreLaunchModule() override {
    // Guest KeDelay and host 1 ms sleeps need a 1 ms timer period; otherwise
    // Windows coalesces them near ~15.6 ms and Present cadence goes bimodal.
    nba_settings::FlushPending();
    EnsureHostTimerResolution();
    DisableProcessPowerThrottling();
    REXLOG_INFO("Game profile: resolution={}, keyboard={}, fuzzy_alpha={}, gpu_poll_backoff={}, accurate_gamma={}, force_full_license={}, frame_limit={}, perf_hud={}",
                rex::cvar::GetFlagByName("resolution"),
                rex::cvar::GetFlagByName("mnk_mode"),
                rex::cvar::GetFlagByName("use_fuzzy_alpha_epsilon"),
                rex::cvar::GetFlagByName("nba_gpu_poll_backoff"),
                rex::cvar::GetFlagByName("gamma_render_target_as_unorm16"),
                rex::cvar::GetFlagByName("nba_force_full_license"),
                rex::cvar::GetFlagByName("nba_frame_limit"),
                rex::cvar::GetFlagByName("nba_perf_hud"));
#ifdef REXGLUE_ENABLE_PERF_COUNTERS
    // The pinned SDK exposes perf_log_csv but does not open the file in its
    // app startup path. Open it before guest threads can emit frame counters.
    const auto csv_path = rex::cvar::GetFlagByName("perf_log_csv");
    if (!csv_path.empty()) {
      rex::perf::SetCsvLogPath(csv_path);
    }
#endif
  }

  void OnPostInitLogging() override {
    InstallLangOverrideLogFilter();
    REXLOG_INFO("Build: version={} revision={} configured_utc={} cpu={} id={}",
                nba_build::kVersion, nba_build::kRevision, nba_build::kConfiguredAtUtc,
                nba_build::kCpuVariant, nba_build::kToken);
    nba_settings::FlushStartupLog();
    nba_settings::ReportMissingGameFiles();
  }

  void OnShutdown() override {
    ReportSuppressedLangOverrideWarnings();
    nba_pause::Release();
    DestroyPerfHud();
    host_overlay_.reset();
  }

 private:
  std::unique_ptr<NbaHostOverlay> host_overlay_;
};
