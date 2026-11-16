#include "host_timer_resolution.h"

#include <atomic>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

void EnsureHostTimerResolution() {
#if defined(_WIN32)
  // Guest KeDelayExecutionThread and host Sleep/sleep_for(1ms) coalesce to the
  // system timer period. The default ~15.6 ms period turns short guest waits
  // into ~16 ms stalls on the Present path (classic 16/33 ms bimodal cadence).
  static std::atomic_flag raised = ATOMIC_FLAG_INIT;
  if (!raised.test_and_set(std::memory_order_relaxed)) {
    timeBeginPeriod(1);
  }
#endif
}
