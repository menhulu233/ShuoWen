#include <windows.h>

#include "app.h"
#include "config.h"
#include "util/log.h"

using namespace shuowen;

namespace {

bool ForwardToRunningEngine() {
  HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return false;
  const std::string request =
      "{\"id\":1,\"cmd\":\"app.open\",\"args\":{\"window\":\"settings\"}}\n";
  DWORD written = 0;
  WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()), &written,
            nullptr);
  CloseHandle(pipe);
  return true;
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
  HANDLE single = CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
  if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
    ForwardToRunningEngine();
    CloseHandle(single);
    return 0;
  }

  LogF("engine: starting v%s", kVersion);
  shuowen::App app;
  if (!app.Initialize(instance)) {
    Log("engine: initialization failed");
    if (single) {
      ReleaseMutex(single);
      CloseHandle(single);
    }
    return 1;
  }

  const int code = app.Run();
  if (single) {
    ReleaseMutex(single);
    CloseHandle(single);
  }
  return code;
}
