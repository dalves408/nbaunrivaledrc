#pragma once

// Raise the Windows multimedia timer period to 1 ms once per process.
// Safe/no-op on non-Windows hosts.
void EnsureHostTimerResolution();
