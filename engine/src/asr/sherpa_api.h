#pragma once

#include <filesystem>
#include <string>

#include "sherpa-onnx/c-api/c-api.h"

namespace shuowen {

// Resolves the sherpa-onnx C ABI at runtime so the engine can start (and report
// a useful error) on machines without the native DLLs, and so upgrading the
// inference core never requires relinking the engine.
struct SherpaApi {
  void* module = nullptr;

  decltype(&::SherpaOnnxCreateOnlineRecognizer) CreateOnlineRecognizer = nullptr;
  decltype(&::SherpaOnnxDestroyOnlineRecognizer) DestroyOnlineRecognizer = nullptr;
  decltype(&::SherpaOnnxCreateOnlineStream) CreateOnlineStream = nullptr;
  decltype(&::SherpaOnnxDestroyOnlineStream) DestroyOnlineStream = nullptr;
  decltype(&::SherpaOnnxOnlineStreamAcceptWaveform) OnlineStreamAcceptWaveform = nullptr;
  decltype(&::SherpaOnnxIsOnlineStreamReady) IsOnlineStreamReady = nullptr;
  decltype(&::SherpaOnnxDecodeOnlineStream) DecodeOnlineStream = nullptr;
  decltype(&::SherpaOnnxGetOnlineStreamResult) GetOnlineStreamResult = nullptr;
  decltype(&::SherpaOnnxDestroyOnlineRecognizerResult) DestroyOnlineRecognizerResult = nullptr;
  decltype(&::SherpaOnnxOnlineStreamInputFinished) OnlineStreamInputFinished = nullptr;

  decltype(&::SherpaOnnxCreateVoiceActivityDetector) CreateVoiceActivityDetector = nullptr;
  decltype(&::SherpaOnnxDestroyVoiceActivityDetector) DestroyVoiceActivityDetector = nullptr;
  decltype(&::SherpaOnnxVoiceActivityDetectorAcceptWaveform) VoiceActivityDetectorAcceptWaveform = nullptr;
  decltype(&::SherpaOnnxVoiceActivityDetectorEmpty) VoiceActivityDetectorEmpty = nullptr;
  decltype(&::SherpaOnnxVoiceActivityDetectorDetected) VoiceActivityDetectorDetected = nullptr;
  decltype(&::SherpaOnnxVoiceActivityDetectorPop) VoiceActivityDetectorPop = nullptr;
  decltype(&::SherpaOnnxVoiceActivityDetectorReset) VoiceActivityDetectorReset = nullptr;
  decltype(&::SherpaOnnxVoiceActivityDetectorFlush) VoiceActivityDetectorFlush = nullptr;
  decltype(&::SherpaOnnxGetVersionStr) GetVersionStr = nullptr;

  bool Load(const std::filesystem::path& search_dir, std::string* error);
  void Unload();
  bool loaded() const { return module != nullptr; }
};

}  // namespace shuowen
