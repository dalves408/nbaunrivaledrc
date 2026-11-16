// Pause control for the F1 settings / Esc quit overlays.
//
// The guest cannot be Win32-suspended safely (a suspended guest thread can hold
// host locks the UI thread needs), so pausing is cooperative:
//   * the title's Present hook (frame_times.cpp) blocks in WaitIfPaused() right
//     after the frame is submitted, which stops the game's frame/logic loop;
//   * the SDK AudioSystem is paused (Pause/Resume are exported) from a helper
//     thread so the UI thread can never block on the audio worker;
//   * the overlay (ImGui on the UI thread / presenter) keeps drawing because
//     it never waits on the guest.
#pragma once

namespace nba_pause {

// UI thread. Pause or resume the game. Idempotent.
void Set(bool paused);
bool IsPaused();

// Guest Present thread. Blocks while paused (returns true if it had to wait).
bool WaitIfPaused();

// Wake everything permanently (quit / window close while paused).
void Release();

}  // namespace nba_pause
