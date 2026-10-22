#pragma once

#include <deque>
#include <string>

#include "asr/sherpa_api.h"
#include "models/paths.h"
#include "store/settings.h"

namespace shuowen {

// Streaming zipformer2 transducer + VAD, using the sherpa-onnx C ABI.
// Silero VAD is used when its ONNX file is present; otherwise an energy VAD
// keeps OnCall usable without any extra model.
class SherpaEngine {
 public:
  SherpaEngine() = default;
  ~SherpaEngine();

  SherpaEngine(const SherpaEngine&) = delete;
  SherpaEngine& operator=(const SherpaEngine&) = delete;

  bool Load(const ModelPaths& paths, VadKind vad, std::string* error);
  void Unload();
  bool ready() const { return recognizer_ != nullptr; }

  void Accept(const float* samples, size_t count);
  std::string Partial();
  std::string Finalize();
  void ResetStream();

  void VadAccept(const float* samples, size_t count);
  bool VadSpeech() const;
  bool VadHasSegment() const;
  void VadPopSegment();
  void VadFlush();
  void VadReset();
  std::string VadName() const;
  std::string Version() const;

 private:
  const char* Intern(const std::string& value);
  void DecodeReady();
  std::string ResultText();

  SherpaApi api_;
  std::deque<std::string> strings_;
  const SherpaOnnxOnlineRecognizer* recognizer_ = nullptr;
  const SherpaOnnxOnlineStream* stream_ = nullptr;
  const SherpaOnnxVoiceActivityDetector* vad_ = nullptr;

  bool energy_vad_ = false;
  float noise_floor_ = 0.006f;
  bool speech_ = false;
  bool segment_ready_ = false;
  double speech_ms_ = 0.0;
  double silence_ms_ = 0.0;

  std::string version_;
};

}  // namespace shuowen
