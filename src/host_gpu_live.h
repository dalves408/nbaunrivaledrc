// Live GPU state changes the SDK exposes no exported API for.
#pragma once

namespace nba_gpu_live {

// Switch the guest-swap post effect (cvar values: "none", "fxaa",
// "fxaa_extreme") on the running command processor. Takes effect on the next
// guest swap. Returns false when the GPU backend is not reachable.
bool SetSwapPostEffect(const char* cvar_value);

}  // namespace nba_gpu_live
