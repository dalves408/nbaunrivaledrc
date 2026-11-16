#pragma once

namespace rex::ui {
class ImGuiDrawer;
class Window;
}  // namespace rex::ui

// Test-build performance HUD: FPS + frame time (guest Present cadence),
// process/system CPU %, NVIDIA GPU % / clock / P-state / power / throttle via
// NVML (nvml.dll loaded dynamically). Values refresh every 0.5 s on a sampler
// thread; the draw path only copies a snapshot. Off unless --nba_perf_hud=true
// or toggled with F3 (when --nba_perf_hud_hotkey is enabled).
void PerfHudOnGuestPresent();
// Drop the previous-Present timestamp (after a pause) so the stall is not
// reported as one huge frame. Call from the guest Present thread.
void PerfHudReset();
void CreatePerfHud(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window);
void DestroyPerfHud();
