#include "dictation.h"

#include <algorithm>
#include <cmath>

#include "config.h"
#include "util/log.h"

namespace shuowen {

Dictation::Dictation(AsrBackend& engine, DictationMode mode)
    : engine_(engine), mode_(mode) {
  if (mode == DictationMode::OnCall) capturing_.store(true);
}

Dictation::~Dictation() { Stop(); }

void Dictation::Start() {
  if (running_.load()) return;
  running_.store(true);
  worker_ = std::thread([this] { WorkerLoop(); });
  Log("dictation: worker started");
}

void Dictation::Stop() {
  if (!running_.exchange(false)) return;
  frames_.Complete();
  if (worker_.joinable()) worker_.join();
  Log("dictation: worker stopped");
}

void Dictation::SetMode(DictationMode mode) {
  const DictationMode previous = mode_.exchange(mode);
  if (previous == DictationMode::OnCall && mode != DictationMode::OnCall) {
    flush_requested_.store(true);
    capturing_.store(false);
  } else if (mode == DictationMode::OnCall) {
    capturing_.store(true);
  } else {
    capturing_.store(false);
  }
  LogF("dictation: mode=%s", mode == DictationMode::Paste
                                 ? "paste"
                                 : mode == DictationMode::Type ? "type"
                                                               : "oncall");
}

void Dictation::SetPaused(bool paused) {
  if (paused && !paused_.exchange(true)) flush_requested_.store(true);
  if (!paused) paused_.store(false);
}

void Dictation::PushFrame(const float* samples, size_t count) {
  if (!running_.load() || !samples || count == 0) return;
  std::vector<float> frame(samples, samples + count);
  frames_.Push(std::move(frame));
}

void Dictation::BeginHold() {
  if (mode_.load() == DictationMode::OnCall) return;
  capturing_.store(true);
  listening_.store(true);
}

void Dictation::EndHold() {
  if (mode_.load() == DictationMode::OnCall) return;
  // A redundant stop (e.g. the UI closing while the hotkey already ended the
  // utterance) must not finalize an empty second time and wipe the "done" HUD.
  if (!capturing_.exchange(false)) return;
  flush_requested_.store(true);
}

void Dictation::Cancel() {
  cancel_requested_.store(true);
  capturing_.store(false);
  flush_requested_.store(true);
}

void Dictation::PushPreroll(const std::vector<float>& frame) {
  for (float sample : frame) preroll_.push_back(sample);
  const size_t max_samples =
      static_cast<size_t>(kSampleRate) * kPrerollMs / 1000;
  while (preroll_.size() > max_samples) preroll_.pop_front();
}

std::vector<float> Dictation::DrainPreroll() {
  std::vector<float> out(preroll_.begin(), preroll_.end());
  preroll_.clear();
  return out;
}

void Dictation::EmitPartial() {
  const auto now = std::chrono::steady_clock::now();
  if (now - last_partial_at_ < std::chrono::milliseconds(kPartialIntervalMs)) {
    return;
  }
  last_partial_at_ = now;
  const std::string text = engine_.Partial();
  if (text == last_partial_) return;
  last_partial_ = text;
  if (on_partial_) on_partial_(text, level_);
}

void Dictation::FinalizeUtterance(bool end_of_hold) {
  const std::string mode_code = ModeToHistoryCode(mode_.load());
  if (!engine_.ready()) {
    if (end_of_hold && on_final_) on_final_({}, mode_code, true);
    return;
  }

  const bool cancelled = cancel_requested_.exchange(false);
  std::string text;
  if (cancelled) {
    engine_.ResetStream();
  } else {
    text = engine_.Finalize();
  }
  while (engine_.VadHasSegment()) engine_.VadPopSegment();
  speech_active_ = false;
  last_partial_.clear();
  level_ = 0.0f;
  listening_.store(false);

  if (!text.empty() || end_of_hold) {
    if (on_final_) on_final_(text, mode_code, end_of_hold);
  }
}

void Dictation::WorkerLoop() {
  std::vector<float> frame;
  while (running_.load()) {
    const bool got = frames_.Pop(frame, 50);
    if (!running_.load()) break;
    if (got && !frame.empty()) PushPreroll(frame);

    const auto now = std::chrono::steady_clock::now();
    if (got && !frame.empty()) {
      double sum = 0.0;
      for (float sample : frame) sum += sample * sample;
      const float rms =
          static_cast<float>(std::sqrt(sum / static_cast<double>(frame.size())));
      level_ = level_ * 0.75f + std::min(1.0f, rms * 4.0f) * 0.25f;
      if (now - last_level_at_ >= std::chrono::milliseconds(kLevelIntervalMs)) {
        last_level_at_ = now;
        if (on_level_) on_level_(level_);
      }
    }

    const bool capturing = capturing_.load() && !paused_.load();
    const bool ptt = mode_.load() != DictationMode::OnCall;

    if (capturing && got && !frame.empty()) {
      if (ptt) {
        if (!speech_active_) {
          speech_active_ = true;
          listening_.store(true);
          utterance_start_ = now;
          last_partial_at_ = {};
          engine_.VadReset();
          auto pre = DrainPreroll();
          if (!pre.empty()) engine_.Accept(pre.data(), pre.size());
        }
        engine_.Accept(frame.data(), frame.size());
        engine_.VadAccept(frame.data(), frame.size());
        EmitPartial();

        if (mode_.load() == DictationMode::Type &&
            now - utterance_start_ >= std::chrono::milliseconds(kPttSegmentAfterMs) &&
            !engine_.VadSpeech() && engine_.VadHasSegment()) {
          FinalizeUtterance(false);
        }
      } else {
        engine_.VadAccept(frame.data(), frame.size());
        const bool now_speech = engine_.VadSpeech();
        if (now_speech && !speech_active_) {
          speech_active_ = true;
          listening_.store(true);
          last_partial_at_ = {};
          auto pre = DrainPreroll();
          if (!pre.empty()) engine_.Accept(pre.data(), pre.size());
        }
        if (speech_active_) {
          engine_.Accept(frame.data(), frame.size());
          EmitPartial();
          if (!now_speech && engine_.VadHasSegment()) {
            FinalizeUtterance(false);
          }
        } else {
          while (engine_.VadHasSegment()) engine_.VadPopSegment();
        }
      }
    }

    if (flush_requested_.exchange(false)) {
      engine_.VadFlush();
      FinalizeUtterance(true);
    }
  }
}

}  // namespace shuowen
