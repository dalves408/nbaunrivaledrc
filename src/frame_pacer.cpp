#include "frame_pacer.h"

#include <rex/cvar.h>
#include <rex/logging/macros.h>

#include <atomic>
#include <cstdint>

namespace {
std::atomic<int> g_current_fps{0};
}  // namespace
int FramePacerCurrentFps() { return g_current_fps.load(std::memory_order_relaxed); }

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <immintrin.h>
#endif

REXCVAR_DEFINE_INT32(nba_frame_limit, 60, "NBA Unrivaled",
                     "Pace guest Present to this many frames per second (0 = off)");
REXCVAR_DEFINE_INT32(nba_frame_limit_low, 0, "NBA Unrivaled",
                     "Adaptive fallback rate (e.g. 30): when frames keep missing the "
                     "nba_frame_limit budget (power/thermal clamp), pace here instead of "
                     "jittering; return once frames fit again. 0 = off");

#if defined(_WIN32)
namespace {

int64_t QpcNow() {
  LARGE_INTEGER v;
  QueryPerformanceCounter(&v);
  return v.QuadPart;
}

int64_t QpcFrequency() {
  static const int64_t f = [] {
    LARGE_INTEGER v;
    QueryPerformanceFrequency(&v);
    return v.QuadPart;
  }();
  return f;
}

HANDLE PacerTimer() {
  // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Win10 1803+) gives ~0.5 ms wakeups
  // without depending on the global timer period. Fall back to a normal timer.
  static HANDLE timer = [] {
    HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*HIGH_RESOLUTION*/,
                                      TIMER_ALL_ACCESS);
    if (!h) h = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    return h;
  }();
  return timer;
}

// Adaptive state; touched only from the single guest Present thread.
struct Adaptive {
  bool low = false;          // pacing at the fallback rate
  int window_frames = 0;     // frames in the current evaluation window
  int window_misses = 0;     // frames whose work exceeded the high-rate budget
  int64_t window_start = 0;  // QPC at window start
};

// Pacer state; touched only from the single guest Present thread.
Adaptive g_ad;
int64_t g_next = 0;
int64_t g_frame_begin = 0;  // end of the previous pacer wait

}  // namespace
#endif

void FramePacerReset() {
#if defined(_WIN32)
  // Same thread as FramePacerAfterPresent (called right after a pause ends).
  g_ad = Adaptive{};
  g_next = 0;
  g_frame_begin = 0;
#endif
}

void FramePacerAfterPresent() {
#if defined(_WIN32)
  const int32_t high_fps = REXCVAR_GET(nba_frame_limit);
  if (high_fps <= 0) return;
  const int32_t low_fps = REXCVAR_GET(nba_frame_limit_low);
  const int64_t freq = QpcFrequency();
  // Present runs on a single guest thread.
  Adaptive& ad = g_ad;
  int64_t& next = g_next;
  int64_t& frame_begin = g_frame_begin;
  int64_t now = QpcNow();
  // Work time of the frame that just presented (excludes the pacer's own wait).
  const int64_t work = frame_begin ? now - frame_begin : 0;
  if (low_fps > 0 && low_fps < high_fps) {
    const int64_t high_budget = freq / high_fps;
    if (ad.window_start == 0) ad.window_start = now;
    ++ad.window_frames;
    if (work > high_budget + high_budget / 20) ++ad.window_misses;  // 5% slack
    const int64_t elapsed = now - ad.window_start;
    if (!ad.low && elapsed >= freq / 2) {
      // 0.5 s windows at the high rate: drop when >=10% of frames miss.
      if (ad.window_frames >= 8 && ad.window_misses * 10 >= ad.window_frames) {
        ad.low = true;
        REXLOG_INFO("frame pacer: {}/{} frames over the {} fps budget; pacing at {} fps",
                    ad.window_misses, ad.window_frames, high_fps, low_fps);
      }
      ad.window_frames = ad.window_misses = 0;
      ad.window_start = now;
    } else if (ad.low && elapsed >= freq * 3) {
      // 3 s windows at the low rate: go back up only if ~every frame would
      // have fit the high-rate slot (hysteresis against flapping).
      if (ad.window_frames >= 30 && ad.window_misses * 100 <= ad.window_frames) {
        ad.low = false;
        REXLOG_INFO("frame pacer: frames fit again; back to {} fps", high_fps);
      }
      ad.window_frames = ad.window_misses = 0;
      ad.window_start = now;
    }
  } else {
    ad.low = false;
  }
  const int32_t fps = ad.low ? low_fps : high_fps;
  g_current_fps.store(fps, std::memory_order_relaxed);
  const int64_t period = freq / fps;
  if (next == 0 || now - next > period) {
    // First frame, or more than a frame behind (load / GPU stall):
    // resynchronise instead of rushing several frames to "catch up".
    next = now + period;
    frame_begin = now;
    return;
  }
  if (now < next) {
    const int64_t spin_window = freq / 1000;  // final ~1 ms is spun for accuracy
    const int64_t sleep_ticks = (next - now) - spin_window;
    if (sleep_ticks > 0) {
      if (HANDLE timer = PacerTimer()) {
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(sleep_ticks * 10000000 / freq);  // relative, 100 ns
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
          WaitForSingleObject(timer, INFINITE);
        }
      }
    }
    while ((now = QpcNow()) < next) {
      _mm_pause();
    }
  }
  frame_begin = now;
  next += period;
#endif
}

void DisableProcessPowerThrottling() {
#if defined(_WIN32)
  PROCESS_POWER_THROTTLING_STATE state{};
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  // EXECUTION_SPEED (EcoQoS) | IGNORE_TIMER_RESOLUTION, both forced off.
  state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | 0x4;
  state.StateMask = 0;
  if (!SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state))) {
    REXLOG_WARN("Could not opt out of process power throttling (error {})", GetLastError());
  }
#endif
}
