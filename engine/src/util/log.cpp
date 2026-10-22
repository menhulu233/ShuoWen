#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

#include "config.h"
#include "text.h"

namespace shuowen {
namespace {

std::mutex g_mutex;
bool g_initialized = false;

void EnsureFile() {
  if (g_initialized) return;
  g_initialized = true;
  std::error_code ec;
  std::filesystem::create_directories(paths::DataDir() / L"logs", ec);
  const auto file = paths::LogFile();
  std::error_code sizeEc;
  const auto size = std::filesystem::file_size(file, sizeEc);
  if (!sizeEc && size > 5ull * 1024 * 1024) {
    std::filesystem::rename(file, file.wstring() + L".1", ec);
  }
}

void WriteLine(const std::string& message) {
  std::lock_guard<std::mutex> lock(g_mutex);
  EnsureFile();
  SYSTEMTIME st;
  GetLocalTime(&st);
  char stamp[64];
  std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d ",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                st.wMilliseconds);
  std::string line = stamp;
  line += message;
  line += "\r\n";

  HANDLE file = CreateFileW(paths::LogFile().c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
  CloseHandle(file);
}

}  // namespace

void Log(const std::string& message) { WriteLine(message); }

void LogF(const char* format, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  WriteLine(buffer);
}

void LogW(const std::wstring& message) { WriteLine(WideToUtf8(message)); }

}  // namespace shuowen
