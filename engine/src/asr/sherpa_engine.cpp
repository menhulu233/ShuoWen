#include "sherpa_engine.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <thread>
#include <vector>

#include "config.h"
#include "util/log.h"
#include "util/text.h"

namespace shuowen {

SherpaEngine::~SherpaEngine() { Unload(); }

const char* SherpaEngine::Intern(const std::string& value) {
  strings_.push_back(value);
  return strings_.back().c_str();
}

bool SherpaEngine::Load(const ModelPaths& paths, VadKind vad,
                        std::string* error) {
  Unload();
  if (!paths.AsrPresent()) {
    if (error) {
      *error = "模型文件缺失：" + WideToUtf8(paths.TierDir().wstring());
    }
    return false;
  }

  std::string api_error;
  if (!api_.Load(paths::ExeDir(), &api_error)) {
    if (error) *error = api_error;
    return false;
  }
  if (api_.GetVersionStr) version_ = api_.GetVersionStr();

  const std::string encoder = WideToUtf8(paths.Encoder().wstring());
  const std::string decoder = WideToUtf8(paths.Decoder().wstring());
  const std::string joiner = WideToUtf8(paths.Joiner().wstring());
  const std::string tokens = WideToUtf8(paths.Tokens().wstring());

  SherpaOnnxOnlineRecognizerConfig config{};
  config.feat_config.sample_rate = kSampleRate;
  config.feat_config.feature_dim = 80;
  config.model_config.transducer.encoder = Intern(encoder);
  config.model_config.transducer.decoder = Intern(decoder);
  config.model_config.transducer.joiner = Intern(joiner);
  config.model_config.tokens = Intern(tokens);
  config.model_config.num_threads =
      std::clamp(static_cast<int>(std::thread::hardware_concurrency()) / 2, 2, 4);
  config.model_config.provider = Intern("cpu");
  config.model_config.debug = 0;
  config.model_config.model_type = Intern("zipformer2");
  config.model_config.modeling_unit = Intern("cjkchar");
  config.decoding_method = Intern("greedy_search");
  config.max_active_paths = 4;
  config.enable_endpoint = 0;
  config.hotwords_score = 0.0f;

  LogF("asr: loading tier=%dms threads=%d", paths.tier_ms,
       config.model_config.num_threads);
  recognizer_ = api_.CreateOnlineRecognizer(&config);
  if (!recognizer_) {
    if (error) *error = "sherpa 无法创建识别器（模型与引擎版本不匹配？）";
    Unload();
    return false;
  }
  stream_ = api_.CreateOnlineStream(recognizer_);
  if (!stream_) {
    if (error) *error = "sherpa 无法创建识别流";
    Unload();
    return false;
  }

  if (vad == VadKind::Silero && paths.SileroPresent()) {
    // sherpa-onnx 1.13.x aborts inside the V5 graph with "Unsupported silero
    // vad model". Reject the known-incompatible small file before calling into
    // the C API so a stray V5 download cannot take the whole engine down.
    std::error_code ec;
    const auto vad_size = std::filesystem::file_size(paths.SileroVad(), ec);
    if (ec || vad_size < ModelPaths::kMinSileroVadBytes) {
      LogF("vad: unsupported Silero model (%llu bytes); expected V4 (~1.8 MB), "
           "using energy VAD",
           static_cast<unsigned long long>(ec ? 0 : vad_size));
    } else {
      SherpaOnnxVadModelConfig vad_config{};
      vad_config.silero_vad.model =
          Intern(WideToUtf8(paths.SileroVad().wstring()));
      vad_config.silero_vad.threshold = 0.5f;
      vad_config.silero_vad.min_silence_duration = 0.5f;
      vad_config.silero_vad.min_speech_duration = 0.10f;
      vad_config.silero_vad.window_size = 512;
      vad_config.sample_rate = kSampleRate;
      vad_config.num_threads = 1;
      vad_config.provider = Intern("cpu");
      vad_config.debug = 0;
      vad_ = api_.CreateVoiceActivityDetector(&vad_config, 30.0f);
      if (!vad_) {
        Log("vad: Silero init failed, falling back to energy VAD");
      }
    }
  }
  energy_vad_ = vad_ == nullptr;
  LogF("vad: %s", energy_vad_ ? "energy fallback" : "Silero");
  Log("asr: ready");
  return true;
}

void SherpaEngine::Unload() {
  if (vad_ && api_.DestroyVoiceActivityDetector) {
    api_.DestroyVoiceActivityDetector(vad_);
  }
  vad_ = nullptr;
  if (stream_ && api_.DestroyOnlineStream) {
    api_.DestroyOnlineStream(stream_);
  }
  stream_ = nullptr;
  if (recognizer_ && api_.DestroyOnlineRecognizer) {
    api_.DestroyOnlineRecognizer(recognizer_);
  }
  recognizer_ = nullptr;
  api_.Unload();
  strings_.clear();
  energy_vad_ = false;
  speech_ = false;
  segment_ready_ = false;
  speech_ms_ = 0.0;
  silence_ms_ = 0.0;
  noise_floor_ = 0.006f;
}

void SherpaEngine::DecodeReady() {
  if (!recognizer_ || !stream_) return;
  while (api_.IsOnlineStreamReady(recognizer_, stream_)) {
    api_.DecodeOnlineStream(recognizer_, stream_);
  }
}

void SherpaEngine::Accept(const float* samples, size_t count) {
  if (!stream_ || !samples || count == 0) return;
  api_.OnlineStreamAcceptWaveform(stream_, kSampleRate, samples,
                                  static_cast<int32_t>(count));
  DecodeReady();
}

std::string SherpaEngine::ResultText() {
  if (!recognizer_ || !stream_) return {};
  const SherpaOnnxOnlineRecognizerResult* result =
      api_.GetOnlineStreamResult(recognizer_, stream_);
  if (!result) return {};
  std::string text = result->text ? result->text : "";
  api_.DestroyOnlineRecognizerResult(result);
  return WideToUtf8(TrimW(NormalizeCjk(Utf8ToWide(text))));
}

std::string SherpaEngine::Partial() { return ResultText(); }

std::string SherpaEngine::Finalize() {
  if (!stream_) return {};
  // 1.5 s of trailing silence lets the last zipformer2 chunk decode; then
  // InputFinished flushes the remaining tokens.
  std::vector<float> padding(kSampleRate * 3 / 2, 0.0f);
  api_.OnlineStreamAcceptWaveform(stream_, kSampleRate, padding.data(),
                                  static_cast<int32_t>(padding.size()));
  api_.OnlineStreamInputFinished(stream_);
  DecodeReady();
  std::string text = ResultText();
  ResetStream();
  return WideToUtf8(EnsureFinalPunct(Utf8ToWide(text)));
}

void SherpaEngine::ResetStream() {
  if (!recognizer_) return;
  if (stream_ && api_.DestroyOnlineStream) api_.DestroyOnlineStream(stream_);
  stream_ = api_.CreateOnlineStream(recognizer_);
}

void SherpaEngine::VadAccept(const float* samples, size_t count) {
  if (!samples || count == 0) return;
  if (vad_) {
    api_.VoiceActivityDetectorAcceptWaveform(vad_, samples,
                                             static_cast<int32_t>(count));
    return;
  }

  double sum = 0.0;
  for (size_t i = 0; i < count; ++i) sum += samples[i] * samples[i];
  const float rms = static_cast<float>(std::sqrt(sum / count));
  const double ms = 1000.0 * static_cast<double>(count) / kSampleRate;
  const float threshold = std::max(0.006f, noise_floor_ * 3.0f);

  if (rms >= threshold) {
    speech_ms_ += ms;
    silence_ms_ = 0.0;
    if (!speech_ && speech_ms_ >= 80.0) speech_ = true;
  } else {
    silence_ms_ += ms;
    speech_ms_ = 0.0;
    if (!speech_) {
      noise_floor_ = noise_floor_ * 0.98f + rms * 0.02f;
      noise_floor_ = std::clamp(noise_floor_, 0.001f, 0.05f);
    } else if (silence_ms_ >= 500.0) {
      speech_ = false;
      segment_ready_ = true;
    }
  }
}

bool SherpaEngine::VadSpeech() const {
  if (vad_) return api_.VoiceActivityDetectorDetected(vad_) != 0;
  return speech_;
}

bool SherpaEngine::VadHasSegment() const {
  if (vad_) return api_.VoiceActivityDetectorEmpty(vad_) == 0;
  return segment_ready_;
}

void SherpaEngine::VadPopSegment() {
  if (vad_) {
    if (api_.VoiceActivityDetectorEmpty(vad_) == 0) {
      api_.VoiceActivityDetectorPop(vad_);
    }
    return;
  }
  segment_ready_ = false;
}

void SherpaEngine::VadFlush() {
  if (vad_) {
    api_.VoiceActivityDetectorFlush(vad_);
    return;
  }
  if (speech_) {
    speech_ = false;
    segment_ready_ = true;
  }
}

void SherpaEngine::VadReset() {
  if (vad_) {
    api_.VoiceActivityDetectorReset(vad_);
    return;
  }
  speech_ = false;
  segment_ready_ = false;
  speech_ms_ = 0.0;
  silence_ms_ = 0.0;
}

std::string SherpaEngine::VadName() const {
  return energy_vad_ ? "energy" : "silero";
}

std::string SherpaEngine::Version() const { return version_; }

}  // namespace shuowen
