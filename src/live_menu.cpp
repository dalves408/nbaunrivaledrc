#include "generated/default/nbaunrivaled_init.h"

#include <rex/cvar.h>
#include <rex/hook.h>

#include <atomic>
#include <fstream>

// Offline PC ship: Xbox LIVE Game must stay darkened and must never take
// focus. Stock FE_MAINMENU DefaultFocus is Quick Play, but with full license
// / signed-in paths the LIVE NavButton still receives focus and wraps into
// the up/down ring (Challenge.NavUp -> LIVE, Quit.NavDown -> LIVE).
//
// Hard block is patching FRONT_END/FE_MAINMENU*.XUR with
// scripts/title-menu-patches.json (Enabled=false, DefaultFocus=Challenge, NavUp/Down
// rewired so Challenge is the top stop). Keep this sign-in hook as defense
// in depth for any remaining FE enable checks.
// EN-EN FE_MAINMENU.XUS overlays crash this title's XUI loader — do not use them.
REXCVAR_DEFINE_BOOL(nba_disable_live_menu, true, "NBA Unrivaled",
                    "Force XamUserGetSigninState=0 (defense-in-depth for LIVE menu)");

namespace {

void NoteLiveHook() {
  static std::atomic_flag once = ATOMIC_FLAG_INIT;
  if (once.test_and_set(std::memory_order_relaxed)) {
    return;
  }
  REXLOG_INFO("nba_disable_live_menu hook fired: XamUserGetSigninState -> 0");
  std::ofstream marker("private/native-user-xenos/live-menu-hook-fired.txt",
                       std::ios::app);
  if (marker) {
    marker << "XamUserGetSigninState\n";
  }
}

}  // namespace

// Strong replacement for the SDK import used at guest 0x8228F310.
REX_HOOK_RAW(__imp__XamUserGetSigninState) {
  if (!REXCVAR_GET(nba_disable_live_menu)) {
    ctx.r3.u64 = 1;  // signed-in local (stock ReXGlue)
    return;
  }
  NoteLiveHook();
  (void)base;
  ctx.r3.u64 = 0;  // XUSER_SIGNIN_STATE_NOT_SIGNED_IN
}
