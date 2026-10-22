#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "asr/sherpa_engine.h"
#include "audio/frame_queue.h"
#include "store/settings.h"

namespace shuowen {

// Owns the single worker thread that turns mic frames into partial/final text.
// Streaming ASR and VAD are not thread-safe, so everything that touches them
// happens here; the hotkey and WASAPI only flip flags / enqueue frames.
class Dictation {
 public:
  using PartialFn = std::function<void(const std::string&, float)>;
  using FinalFn =
      std::function<void(const std::string&, const std::string&, bool)>;
  using LevelFn = std::function<void(float)>;

  Dictation(SherpaEngine& engine, DictationMode mode);
  ~Dictation();

  Dictation(const Dictation&) = delete;
  Dictation& operator=(const Dictation&) = delete;

  void Start();
  void Stop();

  void SetMode(DictationMode mode);
  void SetPaused(bool paused);
  bool paused() const { return paused_.load(); }

  void PushFrame(const float* samples, size_t count);
  void BeginHold();
  void EndHold();
  void Cancel();

  void OnPartial(PartialFn callback) { on_partial_ = std::move(callback); }
  void OnFinal(FinalFn callback) { on_final_ = std::move(callback); }
  void OnLevel(LevelFn callback) { on_level_ = std::move(callback); }

  bool capturing() const { return capturing_.load(); }
  bool listening() const { return listening_.load(); }

 private:
  void WorkerLoop();
  void FinalizeUtterance(bool end_of_hold);
  void EmitPartial();
  void PushPreroll(const std::vector<float>& frame);
  std::vector<float> DrainPreroll();

  static constexpr int kPrerollMs = 300;
  static constexpr int kPartialIntervalMs = 150;
  static constexpr int kLevelIntervalMs = 50;
  static constexpr int kPttSegmentAfterMs = 4000;

  SherpaEngine& engine_;
  FrameQueue frames_{1024};
  std::thread worker_;
  std::atomic<bool> running_{false};

  std::atomic<DictationMode> mode_;
  std::atomic<bool> capturing_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> flush_requested_{false};
  std::atomic<bool> cancel_requested_{false};
  std::atomic<bool> listening_{false};

  std::deque<float> preroll_;
  bool speech_active_ = false;
  std::string last_partial_;
  std::chrono::steady_clock::time_point last_partial_at_{};
  std::chrono::steady_clock::time_point last_level_at_{};
  std::chrono::steady_clock::time_point utterance_start_{};
  float level_ = 0.0f;

  PartialFn on_partial_;
  FinalFn on_final_;
  LevelFn on_level_;
};

}  // namespace shuowen
