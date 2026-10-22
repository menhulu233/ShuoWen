#include "sherpa_api.h"

#include <windows.h>

#include "util/log.h"
#include "util/text.h"

namespace shuowen {
namespace {

template <typename T>
bool Resolve(HMODULE module, const char* name, T& target, std::string* error) {
  target = reinterpret_cast<T>(GetProcAddress(module, name));
  if (!target) {
    if (error) *error = std::string("missing sherpa-onnx export: ") + name;
    return false;
  }
  return true;
}

}  // namespace

bool SherpaApi::Load(const std::filesystem::path& search_dir,
                     std::string* error) {
  Unload();

  const auto dll = search_dir / L"sherpa-onnx-c-api.dll";
  std::error_code ec;
  if (!std::filesystem::exists(dll, ec)) {
    if (error) {
      *error = "sherpa-onnx-c-api.dll not found in " +
               WideToUtf8(search_dir.wstring());
    }
    return false;
  }

  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
                           LOAD_LIBRARY_SEARCH_USER_DIRS);
  AddDllDirectory(search_dir.c_str());
  module = static_cast<void*>(LoadLibraryExW(
      dll.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS));
  if (!module) {
    if (error) {
      *error = "LoadLibrary(sherpa-onnx-c-api.dll) failed: " +
               std::to_string(GetLastError());
    }
    return false;
  }

  bool ok = true;
  HMODULE handle = static_cast<HMODULE>(module);
  ok &= Resolve(handle, "SherpaOnnxCreateOnlineRecognizer", CreateOnlineRecognizer, error);
  ok &= Resolve(handle, "SherpaOnnxDestroyOnlineRecognizer", DestroyOnlineRecognizer, error);
  ok &= Resolve(handle, "SherpaOnnxCreateOnlineStream", CreateOnlineStream, error);
  ok &= Resolve(handle, "SherpaOnnxDestroyOnlineStream", DestroyOnlineStream, error);
  ok &= Resolve(handle, "SherpaOnnxOnlineStreamAcceptWaveform", OnlineStreamAcceptWaveform, error);
  ok &= Resolve(handle, "SherpaOnnxIsOnlineStreamReady", IsOnlineStreamReady, error);
  ok &= Resolve(handle, "SherpaOnnxDecodeOnlineStream", DecodeOnlineStream, error);
  ok &= Resolve(handle, "SherpaOnnxGetOnlineStreamResult", GetOnlineStreamResult, error);
  ok &= Resolve(handle, "SherpaOnnxDestroyOnlineRecognizerResult", DestroyOnlineRecognizerResult, error);
  ok &= Resolve(handle, "SherpaOnnxOnlineStreamInputFinished", OnlineStreamInputFinished, error);
  ok &= Resolve(handle, "SherpaOnnxCreateVoiceActivityDetector", CreateVoiceActivityDetector, error);
  ok &= Resolve(handle, "SherpaOnnxDestroyVoiceActivityDetector", DestroyVoiceActivityDetector, error);
  ok &= Resolve(handle, "SherpaOnnxVoiceActivityDetectorAcceptWaveform", VoiceActivityDetectorAcceptWaveform, error);
  ok &= Resolve(handle, "SherpaOnnxVoiceActivityDetectorEmpty", VoiceActivityDetectorEmpty, error);
  ok &= Resolve(handle, "SherpaOnnxVoiceActivityDetectorDetected", VoiceActivityDetectorDetected, error);
  ok &= Resolve(handle, "SherpaOnnxVoiceActivityDetectorPop", VoiceActivityDetectorPop, error);
  ok &= Resolve(handle, "SherpaOnnxVoiceActivityDetectorReset", VoiceActivityDetectorReset, error);
  ok &= Resolve(handle, "SherpaOnnxVoiceActivityDetectorFlush", VoiceActivityDetectorFlush, error);
  Resolve(handle, "SherpaOnnxGetVersionStr", GetVersionStr, nullptr);

  if (!ok) {
    Unload();
    return false;
  }

  if (GetVersionStr) {
    LogF("sherpa: loaded %s", GetVersionStr());
  }
  return true;
}

void SherpaApi::Unload() {
  if (module) {
    FreeLibrary(static_cast<HMODULE>(module));
    module = nullptr;
  }
}

}  // namespace shuowen
