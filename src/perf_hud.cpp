// Test-build performance HUD. See perf_hud.h.
#include "perf_hud.h"
#include "frame_pacer.h"

#include <rex/cvar.h>
#include <rex/logging/macros.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window.h>

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(nba_perf_hud, false, "NBA Unrivaled",
                    "Show the test-build FPS / CPU / GPU overlay");
REXCVAR_DEFINE_BOOL(nba_perf_hud_hotkey, true, "NBA Unrivaled",
                    "Allow F3 to toggle the performance overlay");

namespace {

using Clock = std::chrono::steady_clock;

// Written by the guest Present thread, drained by the sampler every 0.5 s.
std::atomic<uint64_t> g_frames{0};
std::atomic<uint64_t> g_frame_us_sum{0};
std::atomic<uint64_t> g_frame_us_max{0};
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_reset{false};

struct Snapshot {
  double fps = 0, avg_ms = 0, max_ms = 0;
  double cpu_process = -1, cpu_system = -1;
  int gpu_util = -1, gpu_mhz = -1, gpu_pstate = -1;
  double gpu_watts = -1;
  unsigned long long gpu_throttle = 0;
  bool have_gpu = false;
};

std::mutex g_snapshot_mutex;
Snapshot g_snapshot;

#if defined(_WIN32)
// Minimal NVML surface, resolved at runtime so no SDK/import lib is needed.
using nvmlDevice_t = void*;
struct nvmlUtilization_t { unsigned int gpu, memory; };
struct Nvml {
  HMODULE dll = nullptr;
  nvmlDevice_t device = nullptr;
  int (*Init)() = nullptr;
  int (*Shutdown)() = nullptr;
  int (*GetHandleByIndex)(unsigned, nvmlDevice_t*) = nullptr;
  int (*GetUtilization)(nvmlDevice_t, nvmlUtilization_t*) = nullptr;
  int (*GetClock)(nvmlDevice_t, int, unsigned*) = nullptr;
  int (*GetPState)(nvmlDevice_t, int*) = nullptr;
  int (*GetPower)(nvmlDevice_t, unsigned*) = nullptr;
  int (*GetThrottle)(nvmlDevice_t, unsigned long long*) = nullptr;

  bool Load() {
    dll = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) dll = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    if (!dll) return false;
    auto sym = [&](const char* a, const char* b = nullptr) -> FARPROC {
      FARPROC p = GetProcAddress(dll, a);
      return (!p && b) ? GetProcAddress(dll, b) : p;
    };
    Init = reinterpret_cast<int (*)()>(sym("nvmlInit_v2", "nvmlInit"));
    Shutdown = reinterpret_cast<int (*)()>(sym("nvmlShutdown"));
    GetHandleByIndex = reinterpret_cast<int (*)(unsigned, nvmlDevice_t*)>(
        sym("nvmlDeviceGetHandleByIndex_v2", "nvmlDeviceGetHandleByIndex"));
    GetUtilization = reinterpret_cast<int (*)(nvmlDevice_t, nvmlUtilization_t*)>(
        sym("nvmlDeviceGetUtilizationRates"));
    GetClock = reinterpret_cast<int (*)(nvmlDevice_t, int, unsigned*)>(sym("nvmlDeviceGetClockInfo"));
    GetPState = reinterpret_cast<int (*)(nvmlDevice_t, int*)>(sym("nvmlDeviceGetPerformanceState"));
    GetPower = reinterpret_cast<int (*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetPowerUsage"));
    GetThrottle = reinterpret_cast<int (*)(nvmlDevice_t, unsigned long long*)>(
        sym("nvmlDeviceGetCurrentClocksEventReasons", "nvmlDeviceGetCurrentClocksThrottleReasons"));
    if (!Init || !GetHandleByIndex || !GetUtilization || Init() != 0) {
      FreeLibrary(dll);
      dll = nullptr;
      return false;
    }
    if (GetHandleByIndex(0, &device) != 0) {
      device = nullptr;
      return false;
    }
    return true;
  }
};

uint64_t FileTimeU64(const FILETIME& ft) {
  return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
#endif

class Sampler {
 public:
  void Start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { Run(); });
  }
  void Stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
  }
  ~Sampler() { Stop(); }

 private:
  void Run() {
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    SetThreadDescription(GetCurrentThread(), L"nba_perf_hud_sampler");
    Nvml nvml;
    const bool have_nvml = nvml.Load() && nvml.device;
    if (!have_nvml) REXLOG_WARN("perf HUD: NVML unavailable; GPU% hidden");
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const double ncpu = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
    FILETIME c, e, k, u, idle, sk, su;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    GetSystemTimes(&idle, &sk, &su);
    uint64_t p_prev = FileTimeU64(k) + FileTimeU64(u);
    uint64_t s_idle_prev = FileTimeU64(idle), s_busy_prev = FileTimeU64(sk) + FileTimeU64(su);
#endif
    auto t_prev = Clock::now();
    while (running_.load(std::memory_order_relaxed)) {
      // 0.5 s window; sleep in small steps so Stop() returns promptly.
      const auto target = t_prev + std::chrono::milliseconds(500);
      while (running_.load(std::memory_order_relaxed) && Clock::now() < target) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      const auto now = Clock::now();
      const double wall_s = std::chrono::duration<double>(now - t_prev).count();
      t_prev = now;
      Snapshot s;
      const uint64_t frames = g_frames.exchange(0);
      const uint64_t sum = g_frame_us_sum.exchange(0);
      const uint64_t mx = g_frame_us_max.exchange(0);
      if (frames) {
        s.fps = frames / wall_s;
        s.avg_ms = sum / 1000.0 / frames;
        s.max_ms = mx / 1000.0;
      }
#if defined(_WIN32)
      GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
      const uint64_t p_now = FileTimeU64(k) + FileTimeU64(u);
      s.cpu_process = (p_now - p_prev) / 1e7 / wall_s / ncpu * 100.0;
      p_prev = p_now;
      GetSystemTimes(&idle, &sk, &su);
      const uint64_t s_idle = FileTimeU64(idle), s_busy = FileTimeU64(sk) + FileTimeU64(su);
      const uint64_t d_total = s_busy - s_busy_prev;  // kernel time includes idle
      const uint64_t d_idle = s_idle - s_idle_prev;
      s.cpu_system = d_total ? 100.0 * (double)(d_total - d_idle) / (double)d_total : 0.0;
      s_idle_prev = s_idle;
      s_busy_prev = s_busy;
      if (have_nvml) {
        nvmlUtilization_t util{};
        if (nvml.GetUtilization(nvml.device, &util) == 0) {
          s.have_gpu = true;
          s.gpu_util = static_cast<int>(util.gpu);
        }
        unsigned v = 0;
        if (nvml.GetClock && nvml.GetClock(nvml.device, 0, &v) == 0) s.gpu_mhz = static_cast<int>(v);
        int ps = -1;
        if (nvml.GetPState && nvml.GetPState(nvml.device, &ps) == 0) s.gpu_pstate = ps;
        if (nvml.GetPower && nvml.GetPower(nvml.device, &v) == 0) s.gpu_watts = v / 1000.0;
        unsigned long long r = 0;
        if (nvml.GetThrottle && nvml.GetThrottle(nvml.device, &r) == 0) s.gpu_throttle = r;
      }
#endif
      std::lock_guard<std::mutex> lock(g_snapshot_mutex);
      g_snapshot = s;
    }
#if defined(_WIN32)
    if (have_nvml && nvml.Shutdown) nvml.Shutdown();
#endif
  }

  std::atomic<bool> running_{false};
  std::thread thread_;
};

Sampler g_sampler;

class PerfHudDialog final : public rex::ui::ImGuiDialog {
 public:
  PerfHudDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window)
      : ImGuiDialog(drawer), window_(window) {
    SetEnabled(REXCVAR_GET(nba_perf_hud));
  }
  ~PerfHudDialog() override {
    g_sampler.Stop();
    if (imgui_drawer()) imgui_drawer()->RemoveDialog(this);
  }

 protected:
  void SetEnabled(bool on) {
    g_enabled.store(on, std::memory_order_relaxed);
    if (on) g_sampler.Start();
  }

  void OnDraw(ImGuiIO& io) override {
#if defined(_WIN32)
    if (REXCVAR_GET(nba_perf_hud_hotkey)) {
      const bool f3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
      bool fg = true;
      if (window_ && window_->GetNativeWindowHandle()) {
        fg = GetForegroundWindow() == static_cast<HWND>(window_->GetNativeWindowHandle());
      }
      if (f3 && !f3_down_ && fg) SetEnabled(!g_enabled.load(std::memory_order_relaxed));
      f3_down_ = f3;
    }
#endif
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    Snapshot s;
    {
      std::lock_guard<std::mutex> lock(g_snapshot_mutex);
      s = g_snapshot;
    }
    char line1[96], line2[96], line3[128];
    std::snprintf(line1, sizeof line1, "FPS %5.1f  %5.2f ms  (max %5.1f)  cap %d", s.fps, s.avg_ms, s.max_ms, FramePacerCurrentFps());
    std::snprintf(line2, sizeof line2, "CPU %4.0f%% proc  %4.0f%% sys", s.cpu_process, s.cpu_system);
    if (s.have_gpu) {
      std::snprintf(line3, sizeof line3, "GPU %3d%%  %4d MHz  P%d  %4.1f W%s", s.gpu_util, s.gpu_mhz,
                    s.gpu_pstate, s.gpu_watts,
                    (s.gpu_pstate >= 5 && s.gpu_util > 50) ? "  LOW-CLOCK!" : ((s.gpu_throttle & 0xE8) ? "  THROTTLE" : ""));  // P5+ under load, or HW slowdown/thermal/power-brake bits
    } else {
      std::snprintf(line3, sizeof line3, "GPU   n/a");
    }
    // Display-only: draw straight onto the foreground draw list. No ImGui
    // window is created, so the HUD can never take focus, set WantCapture* or
    // otherwise gate guest input (and there is no extra empty window).
    const char* lines[3] = {line1, line2, line3};
    const float height = io.DisplaySize.y > 0.0f ? io.DisplaySize.y : 1080.0f;
    const float font_size = std::max(11.0f, 14.0f * height / 1080.0f);
    const float pad = std::max(4.0f, font_size * 0.4f);
    ImFont* font = ImGui::GetFont();
    ImVec2 box(0, 0);
    for (const char* l : lines) {
      const ImVec2 sz = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, l);
      box.x = std::max(box.x, sz.x);
      box.y += sz.y + 2.0f;
    }
    const ImVec2 origin(8.0f, 8.0f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + box.x + pad * 2, origin.y + box.y + pad * 2),
                      IM_COL32(0, 0, 0, 140), 3.0f);
    const ImU32 color = s.max_ms > 20.0 ? IM_COL32(255, 115, 90, 255) : IM_COL32(153, 255, 153, 255);
    float y = origin.y + pad;
    for (int i = 0; i < 3; ++i) {
      dl->AddText(font, font_size, ImVec2(origin.x + pad, y), i == 0 ? color : IM_COL32(235, 235, 235, 255),
                  lines[i]);
      y += font_size + 2.0f;
    }
  }

 private:
  rex::ui::Window* window_ = nullptr;
  bool f3_down_ = false;
};

std::unique_ptr<PerfHudDialog> g_hud;

}  // namespace

void PerfHudOnGuestPresent() {
  // Guest Present runs on one guest thread; a function-local static is enough.
  static Clock::time_point previous{};
  const auto now = Clock::now();
  if (g_reset.exchange(false, std::memory_order_relaxed)) previous = {};
  if (g_enabled.load(std::memory_order_relaxed) && previous.time_since_epoch().count() != 0) {
    const uint64_t us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now - previous).count());
    g_frames.fetch_add(1, std::memory_order_relaxed);
    g_frame_us_sum.fetch_add(us, std::memory_order_relaxed);
    uint64_t prev_max = g_frame_us_max.load(std::memory_order_relaxed);
    while (us > prev_max && !g_frame_us_max.compare_exchange_weak(prev_max, us)) {
    }
  }
  previous = now;
}

void PerfHudReset() { g_reset.store(true, std::memory_order_relaxed); }

void CreatePerfHud(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window) {
  if (!drawer || g_hud) return;
  g_hud = std::make_unique<PerfHudDialog>(drawer, window);
  drawer->AddDialog(g_hud.get());
}

void DestroyPerfHud() { g_hud.reset(); }
