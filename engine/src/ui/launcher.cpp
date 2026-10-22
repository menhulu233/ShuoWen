#include "launcher.h"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <thread>

#include "config.h"
#include "util/log.h"
#include "util/text.h"

namespace shuowen {

Launcher::~Launcher() { ShutdownAll(); }

void Launcher::Reap() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = entries_.begin(); it != entries_.end();) {
    HANDLE process = static_cast<HANDLE>(it->process);
    if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
      CloseHandle(process);
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
}

bool Launcher::IsRunning(const std::string& window) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& entry : entries_) {
    if (entry.window != window) continue;
    HANDLE process = static_cast<HANDLE>(entry.process);
    if (process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) return true;
  }
  return false;
}

bool Launcher::Spawn(const std::string& window) {
  const auto exe = paths::UiExe();
  if (exe.empty()) {
    Log("launcher: shuowen_ui.exe not found next to the engine");
    return false;
  }

  const std::wstring params = L"--window=" + Utf8ToWide(window);
  const std::wstring working_dir = exe.parent_path().wstring();
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
  info.lpVerb = L"open";
  info.lpFile = exe.c_str();
  info.lpParameters = params.c_str();
  info.lpDirectory = working_dir.c_str();
  info.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&info)) {
    LogF("launcher: ShellExecuteEx(%s) failed (%lu)",
         WideToUtf8(exe.wstring()).c_str(), GetLastError());
    return false;
  }

  if (!info.hProcess) {
    LogF("launcher: %s launched without a process handle", window.c_str());
    return true;
  }
  const DWORD child_pid = GetProcessId(info.hProcess);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Entry entry;
    entry.process = info.hProcess;
    entry.pid = child_pid;
    entry.window = window;
    entries_.push_back(std::move(entry));
  }
  LogF("launcher: started %s (pid=%lu)", window.c_str(), child_pid);
  return true;
}

bool Launcher::Open(const std::string& window) {
  Reap();
  if (IsRunning(window)) return true;
  return Spawn(window);
}

bool Launcher::EnsureHud() { return Open("hud"); }

bool Launcher::EnsureOnCall() { return Open("oncall"); }

void Launcher::ShutdownAll() {
  std::vector<Entry> entries;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    entries = entries_;
    entries_.clear();
  }
  for (auto& entry : entries) {
    HANDLE process = static_cast<HANDLE>(entry.process);
    if (!process) continue;
    if (WaitForSingleObject(process, 1500) != WAIT_OBJECT_0) {
      TerminateProcess(process, 0);
    }
    CloseHandle(process);
  }
}

}  // namespace shuowen
