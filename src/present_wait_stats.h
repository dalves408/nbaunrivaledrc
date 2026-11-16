#pragma once

#include <cstdint>

// Per-Present wait accounting filled by the native ring-wait hooks. The Present
// timing hook reads and clears these after each guest Present completes.
struct PresentWaitStats {
  uint64_t ring_wait_ns = 0;
  uint32_t ring_wait_spins = 0;
};

PresentWaitStats& PresentWaitStatsTls();
void PresentWaitStatsReset();
