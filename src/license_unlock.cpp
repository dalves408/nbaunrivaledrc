#include "generated/default/nbaunrivaled_init.h"

#include <rex/cvar.h>
#include <rex/hook.h>

#include <atomic>
#include <fstream>

// Force XBLA full-content license bits. The packaged World XBLA build ships as
// a trial that queries XamContentGetLicenseMask; ReXGlue's license_mask cvar
// alone did not flip this title's trial UI, so the guest query path
// is hooked. sub_820CDF20 treats clear bit0 as trial.
REXCVAR_DEFINE_BOOL(nba_force_full_license, true, "NBA Unrivaled",
                    "Return a full XamContent license mask (disable XBLA trial UI)");

REX_EXTERN(__imp__sub_820E9848);
REX_EXTERN(__imp__sub_820CDF20);

namespace {

void NoteHook(const char* name) {
  static std::atomic_flag once_mask = ATOMIC_FLAG_INIT;
  static std::atomic_flag once_trial = ATOMIC_FLAG_INIT;
  std::atomic_flag* flag =
      (name[0] == 'm') ? &once_mask : &once_trial;
  if (flag->test_and_set(std::memory_order_relaxed)) {
    return;
  }
  REXLOG_INFO("nba_force_full_license hook fired: {}", name);
  std::ofstream marker("private/native-user-xenos/license-hook-fired.txt",
                       std::ios::app);
  if (marker) {
    marker << name << "\n";
  }
}

}  // namespace

// 0x820E9848 is the title thunk that calls XamContentGetLicenseMask.
REX_HOOK_RAW(sub_820E9848) {
  if (!REXCVAR_GET(nba_force_full_license)) {
    __imp__sub_820E9848(ctx, base);
    return;
  }
  NoteHook("mask");
  const uint32_t out_ptr = ctx.r3.u32;
  if (out_ptr) {
    REX_STORE_U32(out_ptr, 0xFFFFFFFFu);
  }
  ctx.r3.u64 = 0;  // ERROR_SUCCESS
}

// Title caches "~license_mask.bit0" as the trial flag at guest boot.
REX_HOOK_RAW(sub_820CDF20) {
  if (!REXCVAR_GET(nba_force_full_license)) {
    __imp__sub_820CDF20(ctx, base);
    return;
  }
  NoteHook("trial");
  ctx.r3.u64 = 0;  // licensed: (~mask bit0) clear when mask bit0 set
}
