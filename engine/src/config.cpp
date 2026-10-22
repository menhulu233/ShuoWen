#include "config.h"

#include <windows.h>
#include <shlobj.h>

#include <system_error>

namespace shuowen::paths {
namespace {

std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR raw = nullptr;
  if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw))) {
    return {};
  }
  std::filesystem::path result(raw);
  CoTaskMemFree(raw);
  return result;
}

}  // namespace

std::filesystem::path ExePath() {
  static const std::filesystem::path cached = [] {
    std::wstring buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                      static_cast<DWORD>(buffer.size()));
    if (length == 0) return std::filesystem::path();
    buffer.resize(length);
    return std::filesystem::path(buffer);
  }();
  return cached;
}

std::filesystem::path ExeDir() { return ExePath().parent_path(); }

std::filesystem::path DataDir() {
  static const std::filesystem::path cached = [] {
    auto roaming = KnownFolder(FOLDERID_RoamingAppData);
    if (roaming.empty()) {
      wchar_t buffer[MAX_PATH];
      DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
      if (length > 0 && length < MAX_PATH) roaming = buffer;
    }
    auto dir = roaming / std::wstring(kAppName);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
  }();
  return cached;
}

std::filesystem::path ModelsWritableRoot() { return DataDir() / L"models"; }

std::filesystem::path BundledModelsRoot() { return ExeDir() / L"models"; }

std::filesystem::path LogFile() { return DataDir() / L"logs" / L"engine.log"; }

std::filesystem::path UiExe() {
  const auto dir = ExeDir();
  const std::filesystem::path candidates[] = {
      dir / L"shuowen_ui.exe",
      dir / L"ui" / L"shuowen_ui.exe",
      dir / L"shuowen_ui" / L"shuowen_ui.exe",
  };
  std::error_code ec;
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate, ec)) return candidate;
  }
  return {};
}

}  // namespace shuowen::paths
