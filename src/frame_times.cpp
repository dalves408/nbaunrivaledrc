#include "generated/default/nbaunrivaled_init.h"
#include "present_wait_stats.h"
#include "frame_pacer.h"
#include "perf_hud.h"
#include "host_pause.h"

#include <rex/cvar.h>
#include <rex/hook.h>

#include <chrono>
#include <fstream>
#include <string>

REXCVAR_DEFINE_STRING(nba_frame_log, "", "NBA Unrivaled",
                      "CSV timing of guest Present routine completions (not display FPS)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_EXTERN(__imp__sub_82237030);

// The only VdSwap call site is inside this title's D3D Present routine.
// Timing the complete original routine works in Release without SDK counters
// or elevated ETW access. It measures guest Present calls, not displayed frames.
REX_HOOK_RAW(sub_82237030) {
  PresentWaitStatsReset();
  __imp__sub_82237030(ctx, base);
  // F1 settings / Esc quit menu: hold the guest here (frame just submitted, so
  // the overlay keeps drawing the last frame) until the menu closes.
  if (nba_pause::WaitIfPaused()) {
    FramePacerReset();
    PerfHudReset();
  }
  // Steady 1/N s cadence (default 60) before the timestamp below, so the CSV and
  // HUD measure the paced interval the player sees.
  FramePacerAfterPresent();
  PerfHudOnGuestPresent();
  const auto& path = REXCVAR_GET(nba_frame_log);
  if (path.empty()) {
    return;
  }
  // Present for this title runs on one guest worker; skip a mutex so the
  // measurement path itself does not add host lock traffic near the 16.7 ms edge.
  static std::ofstream file(path);
  if (!file) {
    static bool reported = false;
    if (!reported) {
      REXLOG_ERROR("Could not open guest Present timing file: {}", path);
      reported = true;
    }
    return;
  }
  static bool first = true;
  static auto previous = std::chrono::steady_clock::now();
  static auto last_flush = previous;
  static std::string buffer;
  buffer.reserve(4096);
  const auto now = std::chrono::steady_clock::now();
  const auto wait = PresentWaitStatsTls();
  if (first) {
    buffer.append("frame_time_us,ring_wait_us,ring_wait_spins\n");
    first = false;
  } else {
    buffer.append(std::to_string(
        std::chrono::duration_cast<std::chrono::microseconds>(now - previous)
            .count()));
    buffer.push_back(',');
    buffer.append(std::to_string(wait.ring_wait_ns / 1000));
    buffer.push_back(',');
    buffer.append(std::to_string(wait.ring_wait_spins));
    buffer.push_back('\n');
  }
  previous = now;
  // Flushing every Present adds host I/O into the next measured interval and
  // can push near-16.7 ms frames over the vsync edge. Buffer and flush on a
  // short cadence instead. ReXApp uses _Exit on close, so keep periodic flushes.
  if (buffer.size() >= 2048 ||
      now - last_flush >= std::chrono::milliseconds{100}) {
    file << buffer;
    file.flush();
    buffer.clear();
    last_flush = now;
  }
}
