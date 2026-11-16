#include "host_pause.h"

#include <rex/audio/audio_system.h>
#include <rex/logging/macros.h>
#include <rex/runtime.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace nba_pause {
namespace {

std::atomic<bool> g_paused{false};
std::atomic<bool> g_released{false};
std::mutex g_mutex;
std::condition_variable g_cv;

rex::audio::AudioSystem* GetAudio() {
  auto* runtime = rex::Runtime::instance();
  if (!runtime) return nullptr;
  // SDLAudioSystem derives (non-virtually) from AudioSystem.
  return static_cast<rex::audio::AudioSystem*>(runtime->audio_system());
}

// Pause/Resume of the SDK audio worker can block until the worker reaches its
// wait, so it runs on its own short-lived thread (never on the UI thread).
void ReconcileAudio(bool pause) {
  std::thread([pause] {
    auto* audio = GetAudio();
    if (!audio) return;
    if (pause) {
      if (g_paused.load() && !audio->is_paused()) {
        audio->Pause();
        REXLOG_INFO("host pause: audio worker paused");
        // Resumed while the pause was still completing.
        if (!g_paused.load() && audio->is_paused()) audio->Resume();
      }
    } else if (audio->is_paused()) {
      audio->Resume();
      REXLOG_INFO("host pause: audio worker resumed");
    }
  }).detach();
}

}  // namespace

bool IsPaused() { return g_paused.load(std::memory_order_acquire); }

void Set(bool paused) {
  if (g_released.load()) paused = false;
  if (g_paused.exchange(paused) == paused) return;
  REXLOG_INFO("host pause: {}", paused ? "paused (guest Present stalled, audio paused)" : "resumed");
  ReconcileAudio(paused);
  if (!paused) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cv.notify_all();
  }
}

bool WaitIfPaused() {
  if (!g_paused.load(std::memory_order_acquire)) return false;
  std::unique_lock<std::mutex> lock(g_mutex);
  while (g_paused.load(std::memory_order_acquire) && !g_released.load()) {
    g_cv.wait_for(lock, std::chrono::milliseconds(50));
  }
  return true;
}

void Release() {
  g_released.store(true);
  g_paused.store(false);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cv.notify_all();
  }
  ReconcileAudio(false);
}

}  // namespace nba_pause
