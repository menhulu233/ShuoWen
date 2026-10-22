#include "download.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

#include "util/log.h"
#include "util/text.h"

namespace shuowen {
namespace {

std::string WinHttpError(const char* what) {
  return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")";
}

struct UrlParts {
  std::wstring host;
  std::wstring path;
  INTERNET_PORT port = 0;
  bool secure = false;
};

bool CrackUrl(const std::string& url, UrlParts* parts, std::string* error) {
  const std::wstring wide = Utf8ToWide(url);
  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0,
                       &components)) {
    if (error) *error = WinHttpError("WinHttpCrackUrl");
    return false;
  }
  parts->host.assign(components.lpszHostName, components.dwHostNameLength);
  parts->path.assign(components.lpszUrlPath, components.dwUrlPathLength);
  if (components.lpszExtraInfo && components.dwExtraInfoLength > 0) {
    parts->path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
  }
  parts->port = components.nPort;
  parts->secure = components.nScheme == INTERNET_SCHEME_HTTPS;
  return true;
}

bool FlattenTierFiles(const std::filesystem::path& tier_dir, int tier_ms,
                      std::string* error) {
  const std::vector<std::wstring> wanted = {
      L"encoder-" + std::to_wstring(tier_ms) + L"ms.onnx",
      L"decoder-" + std::to_wstring(tier_ms) + L"ms.onnx",
      L"joiner-" + std::to_wstring(tier_ms) + L"ms.onnx",
      L"tokens.txt",
  };
  std::error_code ec;
  std::vector<std::filesystem::path> found;
  for (auto it = std::filesystem::recursive_directory_iterator(tier_dir, ec);
       !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (!it->is_regular_file()) continue;
    const auto name = it->path().filename().wstring();
    if (std::find(wanted.begin(), wanted.end(), name) != wanted.end()) {
      found.push_back(it->path());
    }
  }
  for (const auto& source : found) {
    const auto dest = tier_dir / source.filename();
    if (source == dest) continue;
    std::filesystem::rename(source, dest, ec);
    if (ec) {
      std::filesystem::copy_file(source, dest,
                                 std::filesystem::copy_options::overwrite_existing,
                                 ec);
      if (!ec) std::filesystem::remove(source, ec);
    }
  }
  if (ec) {
    if (error) *error = "failed to flatten extracted model files";
    return false;
  }
  return true;
}

bool ExtractTarGz(const std::filesystem::path& archive,
                  const std::filesystem::path& dest_dir, std::string* error) {
  wchar_t system_root[MAX_PATH];
  GetEnvironmentVariableW(L"SystemRoot", system_root, MAX_PATH);
  const std::wstring tar =
      std::wstring(system_root) + L"\\System32\\tar.exe";
  std::wstring command = L"\"" + tar + L"\" -xzf \"" + archive.wstring() +
                         L"\" -C \"" + dest_dir.wstring() + L"\"";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');
  if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
    if (error) *error = WinHttpError("CreateProcess(tar)");
    return false;
  }
  const DWORD wait = WaitForSingleObject(process.hProcess, 10 * 60 * 1000);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  if (wait != WAIT_OBJECT_0 || code != 0) {
    if (error) *error = "tar.exe failed to extract the model archive";
    return false;
  }
  return true;
}

std::string LooseUrl(const std::string& source, int tier_ms,
                     const std::wstring& filename) {
  const std::string name = WideToUtf8(filename);
  const std::string path =
      "deployment/models/chunk-" + std::to_string(tier_ms) + "ms-model/" + name;
  if (source == "huggingface") {
    return "https://huggingface.co/GilgameshWind/X-ASR-zh-en/resolve/main/" + path;
  }
  return "https://www.modelscope.ai/models/Gilgamesh-J/X-ASR-zh-en/resolve/master/" +
         path;
}

}  // namespace

bool HttpDownload(const std::string& url, const std::filesystem::path& dest,
                  const DownloadProgressFn& progress,
                  std::atomic<bool>& cancel, std::string* error) {
  UrlParts parts;
  if (!CrackUrl(url, &parts, error)) return false;

  HINTERNET session = WinHttpOpen(
      L"ShuoWen/0.1 (local voice input)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    if (error) *error = WinHttpError("WinHttpOpen");
    return false;
  }
  HINTERNET connection = WinHttpConnect(session, parts.host.c_str(), parts.port, 0);
  if (!connection) {
    WinHttpCloseHandle(session);
    if (error) *error = WinHttpError("WinHttpConnect");
    return false;
  }
  HINTERNET request = WinHttpOpenRequest(
      connection, L"GET", parts.path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, parts.secure ? WINHTTP_FLAG_SECURE : 0);
  if (!request) {
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (error) *error = WinHttpError("WinHttpOpenRequest");
    return false;
  }

  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect,
                   sizeof(redirect));

  bool ok = false;
  std::error_code ec;
  std::filesystem::create_directories(dest.parent_path(), ec);
  const auto part = dest.wstring() + L".part";

  if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    if (error) *error = WinHttpError("WinHTTP request");
  } else {
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
      if (error) *error = "HTTP " + std::to_string(status);
    } else {
      int64_t total = 0;
      DWORD length = 0;
      size = sizeof(length);
      if (WinHttpQueryHeaders(
              request,
              WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
              WINHTTP_HEADER_NAME_BY_INDEX, &length, &size,
              WINHTTP_NO_HEADER_INDEX)) {
        total = length;
      }

      std::ofstream out(part, std::ios::binary | std::ios::trunc);
      if (!out) {
        if (error) *error = "cannot write " + WideToUtf8(part);
      } else {
        int64_t received = 0;
        std::vector<char> buffer(64 * 1024);
        bool failed = false;
        while (!cancel.load()) {
          DWORD read = 0;
          if (!WinHttpReadData(request, buffer.data(),
                               static_cast<DWORD>(buffer.size()), &read)) {
            if (error) *error = WinHttpError("WinHttpReadData");
            failed = true;
            break;
          }
          if (read == 0) break;
          out.write(buffer.data(), static_cast<std::streamsize>(read));
          if (!out) {
            if (error) *error = "write failed";
            failed = true;
            break;
          }
          received += read;
          if (progress) {
            progress(DownloadProgress{WideToUtf8(dest.filename().wstring()),
                                      received, total, 0, 1});
          }
        }
        out.flush();
        out.close();
        if (!failed && !cancel.load()) {
          if (MoveFileExW(part.c_str(), dest.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            ok = true;
          } else if (error) {
            *error = WinHttpError("MoveFileEx");
          }
        } else if (cancel.load() && error) {
          *error = "cancelled";
        }
      }
    }
  }

  if (!ok) std::filesystem::remove(part, ec);
  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  return ok;
}

bool EnsureTier(const ModelPaths& paths, const std::string& source,
                const DownloadProgressFn& progress, std::atomic<bool>& cancel,
                std::string* error) {
  if (paths.AsrPresent()) return true;
  std::error_code ec;
  std::filesystem::create_directories(paths.TierDir(), ec);

  if (source == "official") {
    const std::string archive_name =
        "chunk-" + std::to_string(paths.tier_ms) + "ms.tar.gz";
    const auto archive = paths.TierDir() / Utf8ToWide(archive_name);
    const std::string url = "https://models.speech.wiki/asr/" + archive_name;
    LogF("download: fetching %s", url.c_str());
    std::string download_error;
    if (HttpDownload(url, archive, progress, cancel, &download_error)) {
      std::string extract_error;
      if (ExtractTarGz(archive, paths.TierDir(), &extract_error) &&
          FlattenTierFiles(paths.TierDir(), paths.tier_ms, &extract_error) &&
          paths.AsrPresent()) {
        std::filesystem::remove(archive, ec);
        return true;
      }
      LogF("download: official archive failed: %s", extract_error.c_str());
    } else {
      LogF("download: official archive failed: %s", download_error.c_str());
    }
    if (cancel.load()) {
      if (error) *error = "cancelled";
      return false;
    }
    Log("download: falling back to loose files");
  }

  // Try the selected source first, then every other known mirror. A failed
  // source only leaves the verified files already moved into place, so the
  // next mirror resumes with the remaining files.
  std::vector<std::string> sources;
  const auto add_source = [&](const std::string& name) {
    if (name.empty()) return;
    if (std::find(sources.begin(), sources.end(), name) == sources.end()) {
      sources.push_back(name);
    }
  };
  add_source(source == "official" ? "modelscope" : source);
  add_source("modelscope");
  add_source("huggingface");

  const auto files = paths.RequiredAsrFiles();
  std::string last_error;
  for (const auto& mirror : sources) {
    if (cancel.load()) {
      if (error) *error = "cancelled";
      return false;
    }
    LogF("download: trying mirror %s", mirror.c_str());
    bool mirror_ok = true;
    for (size_t i = 0; i < files.size(); ++i) {
      if (cancel.load()) {
        if (error) *error = "cancelled";
        return false;
      }
      const auto existing_size = std::filesystem::file_size(files[i], ec);
      if (!ec && existing_size > 0) continue;
      if (!ec) std::filesystem::remove(files[i], ec);
      ec.clear();
      const auto filename = files[i].filename().wstring();
      const std::string url = LooseUrl(mirror, paths.tier_ms, filename);
      LogF("download: %s -> %s", url.c_str(),
           WideToUtf8(files[i].wstring()).c_str());
      auto wrapped = [&](const DownloadProgress& p) {
        if (progress) {
          DownloadProgress copy = p;
          copy.index = static_cast<int>(i);
          copy.count = static_cast<int>(files.size());
          progress(copy);
        }
      };
      std::string file_error;
      if (!HttpDownload(url, files[i], wrapped, cancel, &file_error)) {
        last_error = file_error;
        LogF("download: mirror %s failed: %s", mirror.c_str(),
             file_error.c_str());
        mirror_ok = false;
        break;
      }
    }
    if (mirror_ok && paths.AsrPresent()) return true;
  }
  if (error) {
    *error = last_error.empty() ? "download failed" : last_error;
  }
  return false;
}

bool EnsureVad(const ModelPaths& paths, const DownloadProgressFn& progress,
               std::atomic<bool>& cancel, std::string* error) {
  if (paths.SileroPresent()) return true;
  // sherpa-onnx 1.13.x only supports the V4 graph. The unversioned
  // silero_vad.onnx in the release is V5 and makes the runtime exit with
  // "Unsupported silero vad model", so fetch the versioned V4 asset instead.
  const std::string urls[] = {
      "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/"
      "silero_vad_v4.onnx",
      "https://huggingface.co/k2-fsa/sherpa-onnx/resolve/main/"
      "silero_vad_v4.onnx",
  };
  // Download into the writable model root even when an incompatible or
  // corrupt bundled file exists. Resolve() prefers the writable copy.
  const auto target = paths.writable_root / L"silero_vad.onnx";
  std::string local_error;
  for (const auto& url : urls) {
    if (HttpDownload(url, target, progress, cancel, &local_error)) {
      return true;
    }
    LogF("download: vad source failed: %s", local_error.c_str());
    if (cancel.load()) break;
  }
  if (error) *error = local_error;
  return false;
}

}  // namespace shuowen
