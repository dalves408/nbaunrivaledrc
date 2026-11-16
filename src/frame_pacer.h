#pragma once

// Host frame pacer for the guest Present cadence. With guest/host vsync off the
// title presents as soon as the GPU ring drains, so frame times wander (10-14 ms
// with spikes) instead of a steady 16.7 ms. Called after every guest Present; it
// waits on a high-resolution waitable timer (plus a <1 ms spin tail) until the
// next 1/N s deadline. --nba_frame_limit=0 disables it.
void FramePacerAfterPresent();

// Forget the pacing deadline and adaptive window (call on the Present thread
// after a long stall such as the F1 pause so the stall is not paced/counted).
void FramePacerReset();

// Opt the process out of Windows EcoQoS / power throttling (keeps guest threads
// off forced-efficiency scheduling on hybrid P/E-core CPUs and stops Windows 11
// from ignoring timeBeginPeriod when the window is occluded).
void DisableProcessPowerThrottling();

// Current pacing target (0 when the pacer is off); read by the perf HUD.
int FramePacerCurrentFps();
