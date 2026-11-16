#include "host_restart.h"

#include <rex/cvar.h>
#include <rex/logging/macros.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>

REXCVAR_DEFINE_STRING(nba_restart_wait_handle, "", "NBA Unrivaled", "Internal restart handoff");

namespace {
constexpr wchar_t kWaitArg[] = L" --nba_restart_wait_handle=";
}
#endif

void WaitForNbaRestartParent() {
#ifdef _WIN32
  const auto& value = REXCVAR_GET(nba_restart_wait_handle);
  if (value.empty()) return;
  char* end = nullptr;
  const auto raw_handle = std::strtoull(value.c_str(), &end, 10);
  if (!raw_handle || *end) ExitProcess(1);
  const auto parent = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(raw_handle));
  const DWORD result = WaitForSingleObject(parent, 30000);
  CloseHandle(parent);
  if (result != WAIT_OBJECT_0) ExitProcess(1);
#endif
}

bool StartNbaRestart() {
#ifdef _WIN32
  wchar_t exe[32768];
  const DWORD length = GetModuleFileNameW(nullptr, exe, _countof(exe));
  if (!length || length >= _countof(exe)) return false;

  // Inherit just a synchronization handle. The child waits before opening the game or saves.
  HANDLE parent = nullptr;
  const HANDLE current = GetCurrentProcess();
  if (!DuplicateHandle(current, current, current, &parent, SYNCHRONIZE, TRUE, 0)) return false;
  SIZE_T bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
  std::vector<std::max_align_t> storage((bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
  auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  const bool initialized = InitializeProcThreadAttributeList(attributes, 1, 0, &bytes) != FALSE;
  bool started = false;
  if (initialized && UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                               &parent, sizeof(parent), nullptr, nullptr)) {
    std::wstring args = GetCommandLineW();
    const auto& previous = REXCVAR_GET(nba_restart_wait_handle);
    if (!previous.empty()) {
      const std::wstring suffix = kWaitArg + std::wstring(previous.begin(), previous.end());
      if (args.ends_with(suffix)) args.resize(args.size() - suffix.size());
    }
    args.append(kWaitArg).append(std::to_wstring(reinterpret_cast<uintptr_t>(parent)));
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION child{};
    started = CreateProcessW(exe, args.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT,
                             nullptr, nullptr, &startup.StartupInfo, &child) != FALSE;
    if (started) {
      CloseHandle(child.hThread);
      CloseHandle(child.hProcess);
    } else {
      REXLOG_ERROR("Settings: could not restart game (Windows error {})", GetLastError());
    }
  }
  if (initialized) DeleteProcThreadAttributeList(attributes);
  CloseHandle(parent);
  return started;
#else
  return false;
#endif
}
