#include "host_gpu_live.h"

#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/logging/macros.h>
#include <rex/runtime.h>

#include <cstring>

namespace nba_gpu_live {
namespace {

using rex::graphics::CommandProcessor;

// CommandProcessor::SetDesiredSwapPostEffect is not exported from the GPU
// plugin DLL, but the two state members are protected (not private) in the
// public header. Reach them with the derived-class member-pointer idiom. The
// header is the one the plugin was built from, so the layout matches. The
// command processor only reads swap_post_effect_actual_ (per swap, on its own
// thread; a plain 4-byte enum, so a torn read is impossible).
struct CpAccess : CommandProcessor {
  static SwapPostEffect CommandProcessor::* Desired() { return &CpAccess::swap_post_effect_desired_; }
  static SwapPostEffect CommandProcessor::* Actual() { return &CpAccess::swap_post_effect_actual_; }
};

}  // namespace

bool SetSwapPostEffect(const char* cvar_value) {
  auto* runtime = rex::Runtime::instance();
  if (!runtime || !runtime->graphics_system()) return false;
  auto* gfx = static_cast<rex::graphics::GraphicsSystem*>(runtime->graphics_system());
  CommandProcessor* cp = gfx->command_processor();
  if (!cp) return false;
  CommandProcessor::SwapPostEffect effect = CommandProcessor::SwapPostEffect::kNone;
  if (cvar_value && !_stricmp(cvar_value, "fxaa")) {
    effect = CommandProcessor::SwapPostEffect::kFxaa;
  } else if (cvar_value && !_stricmp(cvar_value, "fxaa_extreme")) {
    effect = CommandProcessor::SwapPostEffect::kFxaaExtreme;
  }
  cp->*CpAccess::Desired() = effect;
  cp->*CpAccess::Actual() = effect;
  REXLOG_INFO("settings: swap post effect -> {} (live)", cvar_value ? cvar_value : "none");
  return true;
}

}  // namespace nba_gpu_live
