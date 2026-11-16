#include "generated/default/nbaunrivaled_init.h"
#include "present_wait_stats.h"

#include <rex/cvar.h>
#include <rex/hook.h>

#include <chrono>
#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// Optional cooperative yield after a long unchanged streak. Default off:
// tip-off is Present/single-thread bound and yielding the poll misses the 16.7 ms
// slot. The always-on win is a native poll that removes empty db16cyc + PPC
// save/restore cost from the ring-wait hot path without Sleep.
REXCVAR_DEFINE_BOOL(nba_gpu_poll_backoff, false, "NBA Unrivaled",
                    "Yield after repeated unchanged GPU progress polls");

REX_EXTERN(__imp__sub_822341B0);
REX_EXTERN(__imp__sub_8223AA18);

PresentWaitStats& PresentWaitStatsTls() {
  static thread_local PresentWaitStats stats;
  return stats;
}

void PresentWaitStatsReset() { PresentWaitStatsTls() = {}; }

namespace {

inline void PausePoll() {
#if defined(_MSC_VER)
  _mm_pause();
#elif defined(__i386__) || defined(__x86_64__)
  __builtin_ia32_pause();
#endif
}

inline void YieldPoll() {
#if defined(_WIN32)
  SwitchToThread();
#endif
}

// Native body of sub_822341B0. Matches generated semantics without the PPC
// savegpr/restgpr theater around empty db16cyc. No host delay is inserted on
// the keep-waiting path (recomp already elided db16cyc); timeout falls through
// to the complete original helper so recovery stays intact.
bool NativeGpuProgressWait(PPCContext& ctx, uint8_t* base, uint32_t wait) {
  const uint32_t device = REX_LOAD_U32(wait + 0);

  if (REX_LOAD_U8(device + 10941) & 0x2) {
    ctx.r3.u64 = 0;
    return false;
  }

  const uint32_t read_ptr_addr = REX_LOAD_U32(device + 10896);
  const uint32_t kpcr = REX_LOAD_U32(ctx.r13.u32 + 256);
  const uint32_t irq_time = REX_LOAD_U32(kpcr + 88);
  const uint32_t last_pos = REX_LOAD_U32(wait + 8);
  const uint32_t cur_pos = REX_LOAD_U32(read_ptr_addr);

  if (last_pos != cur_pos) {
    REX_STORE_U32(wait + 12, irq_time);
    REX_STORE_U32(wait + 8, cur_pos);
  }

  // Inlined sub_82227120.
  const uint32_t misc = REX_LOAD_U32(kpcr + 332);
  if (REX_LOAD_U32(device + 10888) == misc &&
      REX_LOAD_U32(device + 11008) != 0) {
    REX_STORE_U32(wait + 12, irq_time);
  }

  const uint32_t elapsed = irq_time - REX_LOAD_U32(wait + 12);
  if (elapsed < 5000) {
    ctx.r3.u64 = 1;
    return true;
  }

  ctx.r3.u64 = wait;
  __imp__sub_822341B0(ctx, base);
  return ctx.r3.u32 != 0;
}

}  // namespace

// Hot helper used by ring-space waits (0x8223AA18) and Present's in-flight
// frame wait. Replacing the generated body removes empty db16cyc + GPR traffic
// from every poll while preserving return values and recovery.
REX_HOOK_RAW(sub_822341B0) {
  const uint32_t wait = ctx.r3.u32;
  NativeGpuProgressWait(ctx, base, wait);

  if (!REXCVAR_GET(nba_gpu_poll_backoff) || ctx.r3.u32 == 0) {
    return;
  }

  static thread_local struct {
    uint32_t read_pointer = 0;
    uint32_t position = 0;
    unsigned unchanged = 0;
    unsigned pause_streak = 0;
  } history;

  const uint32_t device = REX_LOAD_U32(wait + 0);
  const uint32_t read_pointer = REX_LOAD_U32(device + 10896);
  const uint32_t position = REX_LOAD_U32(read_pointer);
  if (history.read_pointer != read_pointer || history.position != position) {
    history = {read_pointer, position, 0, 0};
    return;
  }
  if (++history.unchanged == 32) {
    history.unchanged = 0;
    PausePoll();
    if (++history.pause_streak == 64) {
      history.pause_streak = 0;
      YieldPoll();
    }
  }
}

// Instrument the original ring-space wait so Present CSV can attribute time,
// then run the complete original body (kick / context / poll / teardown).
REX_HOOK_RAW(sub_8223AA18) {
  auto& stats = PresentWaitStatsTls();
  const auto wait_started = std::chrono::steady_clock::now();
  __imp__sub_8223AA18(ctx, base);
  stats.ring_wait_ns += static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - wait_started)
          .count());
  ++stats.ring_wait_spins;
}
