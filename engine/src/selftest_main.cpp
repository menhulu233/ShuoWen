#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "asr/asr_backend.h"
#include "asr/dictation.h"
#include "asr/sherpa_engine.h"
#include "config.h"
#include "input/hotkey.h"
#include "input/type_output.h"
#include "models/paths.h"
#include "store/history.h"
#include "store/settings.h"
#include "util/json.h"
#include "util/text.h"

namespace {

int g_failures = 0;

// Deterministic stand-in for the sherpa-onnx recognizer: the dictation
// worker loop only needs to observe what it was asked to do.
class FakeAsr : public shuowen::AsrBackend {
 public:
  bool ready() const override { return true; }
  void Accept(const float* samples, size_t count) override {
    (void)samples;
    accepted_samples_ += count;
    ++accept_calls_;
  }
  std::string Partial() override {
    return "p" + std::to_string(++partial_queries_);
  }
  std::string Finalize() override {
    ++finalize_calls_;
    std::lock_guard<std::mutex> lock(final_mutex_);
    const std::string out = final_text_;
    final_text_.clear();
    return out;
  }
  void ResetStream() override { ++reset_calls_; }
  void VadAccept(const float* samples, size_t count) override {
    ++vad_accept_calls_;
    vad_samples_ += count;
    if (!samples || count == 0) return;
    // The test drives the VAD state through the first sample of each frame:
    // 1.0 = speech, 0.0 = silence, -1.0 = silence that closes a segment.
    const float marker = samples[0];
    if (marker > 0.5f) {
      vad_speech_.store(true);
      vad_segment_.store(false);
    } else if (marker < -0.5f) {
      vad_speech_.store(false);
      vad_segment_.store(true);
    } else {
      vad_speech_.store(false);
      vad_segment_.store(false);
    }
  }
  bool VadSpeech() const override { return vad_speech_.load(); }
  bool VadHasSegment() const override { return vad_segment_.load(); }
  void VadPopSegment() override { vad_segment_.store(false); }
  void VadFlush() override { ++vad_flush_calls_; }
  void VadReset() override {
    vad_speech_.store(false);
    vad_segment_.store(false);
  }

  void SetFinalText(const std::string& text) {
    std::lock_guard<std::mutex> lock(final_mutex_);
    final_text_ = text;
  }

  std::string final_text_;
  std::atomic<int> accept_calls_{0};
  std::atomic<int> vad_accept_calls_{0};
  std::atomic<int> finalize_calls_{0};
  std::atomic<int> reset_calls_{0};
  std::atomic<int> vad_flush_calls_{0};
  std::atomic<size_t> accepted_samples_{0};
  std::atomic<size_t> vad_samples_{0};
  std::atomic<bool> vad_speech_{false};
  std::atomic<bool> vad_segment_{false};

 private:
  std::atomic<int> partial_queries_{0};
  std::mutex final_mutex_;
};

bool WaitFor(const std::function<bool()>& pred, int timeout_ms = 3000) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return pred();
}

struct WavData {
  std::vector<float> samples;
  int sample_rate = 0;
};

bool LoadWav(const std::filesystem::path& path, WavData* out,
             std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot open " + path.string();
    return false;
  }
  auto read_u32 = [&]() {
    unsigned char b[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char*>(b), 4);
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) |
           (static_cast<uint32_t>(b[3]) << 24);
  };
  auto read_u16 = [&]() {
    unsigned char b[2] = {0, 0};
    in.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
  };
  char riff[4] = {0};
  char wave[4] = {0};
  in.read(riff, 4);
  (void)read_u32();
  in.read(wave, 4);
  if (!in || std::memcmp(riff, "RIFF", 4) != 0 ||
      std::memcmp(wave, "WAVE", 4) != 0) {
    if (error) *error = "not a wav file: " + path.string();
    return false;
  }
  while (in) {
    char id[4] = {0};
    if (!in.read(id, 4)) break;
    const uint32_t size = read_u32();
    if (std::memcmp(id, "fmt ", 4) == 0) {
      const uint16_t format = read_u16();
      const uint16_t channels = read_u16();
      const int rate = static_cast<int>(read_u32());
      (void)read_u32();  // byte rate
      (void)read_u16();  // block align
      const uint16_t bits = read_u16();
      if (format != 1 || channels != 1 || bits != 16) {
        if (error) *error = "expect 16-bit mono pcm wav";
        return false;
      }
      out->sample_rate = rate;
      if (size > 16) {
        in.seekg(static_cast<std::streamoff>(size - 16), std::ios::cur);
      }
    } else if (std::memcmp(id, "data", 4) == 0) {
      std::vector<int16_t> pcm(size / 2);
      in.read(reinterpret_cast<char*>(pcm.data()),
              static_cast<std::streamsize>(pcm.size() * 2));
      out->samples.resize(pcm.size());
      for (size_t i = 0; i < pcm.size(); ++i) {
        out->samples[i] = static_cast<float>(pcm[i]) / 32768.0f;
      }
      if (out->sample_rate != shuowen::kSampleRate || out->samples.empty()) {
        if (error) *error = "unexpected wav format";
        return false;
      }
      return true;
    } else {
      in.seekg(static_cast<std::streamoff>(size + (size & 1)), std::ios::cur);
    }
  }
  if (error) *error = "wav data chunk missing: " + path.string();
  return false;
}

std::filesystem::path ExeDir() {
  wchar_t buffer[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path EnvPath(const char* name) {
  const char* value = std::getenv(name);
  return value ? std::filesystem::path(value) : std::filesystem::path();
}

std::filesystem::path FindModelsDir() {
  const auto env = EnvPath("SHUOWEN_MODELS_DIR");
  if (!env.empty() && std::filesystem::exists(env)) return env;
  const auto exe = ExeDir();
  const std::filesystem::path candidates[] = {
      exe / L"models", exe / L".." / L".." / L"dist" / L"models",
      exe / L".." / L".." / L".." / L"dist" / L"models"};
  for (const auto& candidate : candidates) {
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) return candidate;
  }
  return {};
}

std::filesystem::path FindWavDir() {
  const auto env = EnvPath("SHUOWEN_TEST_WAV_DIR");
  if (!env.empty() && std::filesystem::exists(env)) return env;
  const auto exe = ExeDir();
  const std::filesystem::path candidates[] = {
      exe / L"tests" / L"fixtures",
      exe / L".." / L".." / L"tests" / L"fixtures",
      exe / L".." / L"engine" / L"tests" / L"fixtures",
      exe / L".." / L".." / L"engine" / L"tests" / L"fixtures"};
  for (const auto& candidate : candidates) {
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) return candidate;
  }
  return {};
}

std::wstring StripForCompare(const std::wstring& text) {
  std::wstring out;
  for (wchar_t ch : text) {
    if (ch == L' ' || ch == L'\t' || ch == L'。' || ch == L'.' ||
        ch == L'，' || ch == L',' || ch == L'！' || ch == L'!' ||
        ch == L'？' || ch == L'?')
      continue;
    out.push_back(ch);
  }
  return out;
}

int EditDistance(const std::wstring& a, const std::wstring& b) {
  std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = static_cast<int>(i);
    for (size_t j = 1; j <= b.size(); ++j) {
      const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
    }
    prev.swap(cur);
  }
  return prev[b.size()];
}

void Check(bool condition, const char* name) {
  if (condition) {
    std::printf("PASS  %s\n", name);
  } else {
    std::printf("FAIL  %s\n", name);
    ++g_failures;
  }
}

void TestJson() {
  std::string error;
  const auto value = shuowen::Json::Parse(
      R"({"a":[1,true,"x"],"b":null,"c":{"d":2.5}})", &error);
  Check(value.is_object(), "json parses object");
  Check(value.Find("a") && value.Find("a")->is_array(), "json array");
  const std::string dumped = value.Dump();
  const auto again = shuowen::Json::Parse(dumped, &error);
  Check(again.Dump() == dumped, "json roundtrip");
  Check(shuowen::Json::Parse("{bad", &error).is_null(), "json rejects invalid");
}

void TestText() {
  using namespace shuowen;
  Check(NormalizeCjk(L"你 好 world .") == L"你好 world.", "cjk de-space");
  Check(NormalizeCjk(L"hello 你好") == L"hello 你好", "latin space kept");
  Check(EnsureFinalPunct(L"你好") == L"你好。", "cjk final punctuation");
  Check(EnsureFinalPunct(L"hello") == L"hello.", "latin final punctuation");
  Check(EnsureFinalPunct(L"你好。") == L"你好。", "existing punctuation kept");
  Check(CommonPrefixCodePoints(L"你好世界", L"你好🙂") == 2,
        "common prefix by code point");
  Check(CodePointSuffix(L"a🙂b", 1) == L"🙂b", "suffix by code point");
  Check(HotkeyName(0xA3) == "RightCtrl", "hotkey name");
}

void TestHotkeyMatching() {
  using namespace shuowen;
  Check(OwnModBit(VK_RCONTROL) == GlobalHotkey::kModCtrl,
        "own mod right ctrl");
  Check(OwnModBit(VK_LCONTROL) == GlobalHotkey::kModCtrl, "own mod left ctrl");
  Check(OwnModBit(VK_LWIN) == GlobalHotkey::kModWin, "own mod left win");
  Check(OwnModBit('A') == 0, "own mod plain key");

  // The binding modifiers are the extra modifiers required besides the
  // trigger key itself; the default binding is right Ctrl with none.
  Check(HotkeyMatches(VK_RCONTROL, VK_RCONTROL, 0, GlobalHotkey::kModCtrl),
        "right ctrl binding matches");
  Check(!HotkeyMatches(VK_LCONTROL, VK_RCONTROL, 0, GlobalHotkey::kModCtrl),
        "left ctrl does not match right ctrl binding");
  Check(!HotkeyMatches(VK_RCONTROL, VK_RCONTROL, 0,
                       GlobalHotkey::kModCtrl | GlobalHotkey::kModShift),
        "extra modifier breaks match");
  Check(HotkeyMatches(VK_RCONTROL, VK_RCONTROL, GlobalHotkey::kModShift,
                      GlobalHotkey::kModCtrl | GlobalHotkey::kModShift),
        "binding with required modifier matches");
  Check(HotkeyMatches('A', 'A', 0, 0), "plain key binding matches");

  Check(CapturedMods(VK_RCONTROL,
                     GlobalHotkey::kModCtrl | GlobalHotkey::kModAlt) ==
            GlobalHotkey::kModAlt,
        "capture excludes trigger modifier");
}

void TestDictationHold() {
  using namespace shuowen;
  FakeAsr asr;
  Dictation dictation(asr, DictationMode::Paste);
  std::atomic<int> partials{0};
  std::atomic<int> finals{0};
  std::string final_text;
  bool final_end = false;
  dictation.OnPartial([&](const std::string&, float) { ++partials; });
  dictation.OnFinal([&](const std::string& text, const std::string&, bool end) {
    final_text = text;
    final_end = end;
    ++finals;
  });

  dictation.Start();
  std::vector<float> frame(480, 0.1f);
  dictation.BeginHold();
  dictation.PushFrame(frame.data(), frame.size());
  Check(WaitFor([&] { return partials.load() >= 1; }),
        "hold emits partial");

  asr.SetFinalText("你好。");
  dictation.EndHold();
  Check(WaitFor([&] { return finals.load() == 1; }), "hold emits one final");
  Check(final_text == "你好。", "final text comes from backend");
  Check(final_end, "final reports end_of_hold");
  Check(asr.finalize_calls_.load() == 1, "finalize called once");

  dictation.EndHold();
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  Check(finals.load() == 1, "redundant end hold does not finalize twice");
  dictation.Stop();
}

void TestDictationThrottle() {
  using namespace shuowen;
  FakeAsr asr;
  Dictation dictation(asr, DictationMode::Paste);
  std::atomic<int> partials{0};
  dictation.OnPartial([&](const std::string&, float) { ++partials; });
  dictation.Start();
  dictation.BeginHold();

  std::vector<float> frame(480, 0.1f);
  for (int i = 0; i < 5; ++i) dictation.PushFrame(frame.data(), frame.size());
  Check(WaitFor([&] { return asr.accept_calls_.load() >= 5; }),
        "burst frames reach backend");
  Check(partials.load() == 1, "partial throttled inside 150ms");

  std::this_thread::sleep_for(std::chrono::milliseconds(180));
  dictation.PushFrame(frame.data(), frame.size());
  Check(WaitFor([&] { return partials.load() == 2; }),
        "partial resumes after throttle window");
  dictation.Cancel();
  dictation.Stop();
}

void TestDictationPreroll() {
  using namespace shuowen;
  FakeAsr asr;
  Dictation dictation(asr, DictationMode::Paste);
  dictation.Start();
  std::vector<float> frame(480, 0.1f);
  for (int i = 0; i < 5; ++i) dictation.PushFrame(frame.data(), frame.size());
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  Check(asr.accept_calls_.load() == 0,
        "frames pushed before hold are not accepted");

  dictation.BeginHold();
  dictation.PushFrame(frame.data(), frame.size());
  Check(WaitFor([&] { return asr.accepted_samples_.load() >= 6 * 480; }),
        "preroll samples replayed into backend");
  dictation.Cancel();
  dictation.Stop();
}

void TestDictationCancel() {
  using namespace shuowen;
  FakeAsr asr;
  Dictation dictation(asr, DictationMode::Paste);
  std::atomic<int> finals{0};
  std::string final_text = "sentinel";
  bool final_end = false;
  dictation.OnFinal([&](const std::string& text, const std::string&, bool end) {
    final_text = text;
    final_end = end;
    ++finals;
  });
  dictation.Start();
  std::vector<float> frame(480, 0.1f);
  dictation.BeginHold();
  dictation.PushFrame(frame.data(), frame.size());
  Check(WaitFor([&] { return asr.accept_calls_.load() >= 1; }), "accepted");

  asr.SetFinalText("不应出现");
  dictation.Cancel();
  Check(WaitFor([&] { return finals.load() == 1; }), "cancel emits final");
  Check(final_text.empty(), "cancel final carries empty text");
  Check(final_end, "cancel final reports end_of_hold");
  Check(asr.finalize_calls_.load() == 0, "cancel skips finalize");
  Check(asr.reset_calls_.load() >= 1, "cancel resets stream");
  dictation.Stop();
}

void TestDictationPause() {
  using namespace shuowen;
  FakeAsr asr;
  Dictation dictation(asr, DictationMode::Paste);
  dictation.Start();
  dictation.SetPaused(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  const int accepts_before = asr.accept_calls_.load();
  const int vads_before = asr.vad_accept_calls_.load();

  std::vector<float> frame(480, 0.1f);
  dictation.BeginHold();
  dictation.PushFrame(frame.data(), frame.size());
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  Check(asr.accept_calls_.load() == accepts_before,
        "paused dictation does not accept audio");
  Check(asr.vad_accept_calls_.load() == vads_before,
        "paused dictation does not feed VAD");

  dictation.SetPaused(false);
  dictation.PushFrame(frame.data(), frame.size());
  Check(WaitFor([&] { return asr.accept_calls_.load() > accepts_before; }),
        "resume accepts audio again");
  dictation.Cancel();
  dictation.Stop();
}

void TestDictationOnCall() {
  using namespace shuowen;
  FakeAsr asr;
  Dictation dictation(asr, DictationMode::OnCall);
  std::atomic<int> finals{0};
  std::vector<std::string> texts;
  dictation.OnFinal([&](const std::string& text, const std::string&, bool) {
    texts.push_back(text);
    ++finals;
  });
  dictation.Start();
  std::vector<float> speech(480, 0.1f);
  speech[0] = 1.0f;   // VAD marker: speech
  std::vector<float> silence(480, 0.0f);
  std::vector<float> segment_end(480, 0.0f);
  segment_end[0] = -1.0f;  // VAD marker: silence closing a segment

  dictation.PushFrame(speech.data(), speech.size());
  Check(WaitFor([&] { return asr.accept_calls_.load() >= 1; }),
        "oncall accepts speech");

  asr.SetFinalText("第一段");
  dictation.PushFrame(segment_end.data(), segment_end.size());
  Check(WaitFor([&] { return finals.load() == 1; }),
        "oncall finalizes first segment");

  dictation.PushFrame(silence.data(), silence.size());
  dictation.PushFrame(speech.data(), speech.size());
  Check(WaitFor([&] { return asr.accept_calls_.load() >= 3; }),
        "oncall accepts the second segment");
  asr.SetFinalText("第二段");
  dictation.PushFrame(segment_end.data(), segment_end.size());
  Check(WaitFor([&] { return finals.load() == 2; }),
        "oncall finalizes second segment");
  Check(texts.size() == 2 && texts[0] == "第一段" && texts[1] == "第二段",
        "oncall finals do not concatenate");
  dictation.Stop();
}

bool ReplayAsr(const std::filesystem::path& models,
               const std::filesystem::path& wav,
               const std::string& expected_utf8, std::string* final_utf8,
               std::string* error) {
  using namespace shuowen;
  WavData audio;
  if (!LoadWav(wav, &audio, error)) return false;

  auto paths = ModelPaths::ForTier(960);
  paths.writable_root = models;
  paths.bundled_root = models;
  if (!paths.AsrPresent()) {
    if (error) *error = "model files missing in " + models.string();
    return false;
  }

  SherpaEngine engine;
  std::string load_error;
  if (!engine.Load(paths, VadKind::Silero, &load_error)) {
    if (error) *error = load_error;
    return false;
  }

  Dictation dictation(engine, DictationMode::Paste);
  std::atomic<bool> got_final{false};
  std::string text;
  dictation.OnFinal([&](const std::string& value, const std::string&, bool) {
    text = value;
    got_final = true;
  });
  dictation.Start();
  dictation.BeginHold();
  const size_t chunk = 480;  // 30 ms at 16 kHz
  for (size_t offset = 0; offset < audio.samples.size(); offset += chunk) {
    const size_t count = std::min(chunk, audio.samples.size() - offset);
    dictation.PushFrame(audio.samples.data() + offset, count);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  dictation.EndHold();
  const bool ok = WaitFor([&] { return got_final.load(); }, 8000);
  dictation.Stop();
  engine.Unload();
  if (!ok) {
    if (error) *error = "no final within 8s";
    return false;
  }
  if (final_utf8) *final_utf8 = text;

  const std::wstring actual = StripForCompare(Utf8ToWide(text));
  const std::wstring expected = StripForCompare(Utf8ToWide(expected_utf8));
  const int distance = EditDistance(actual, expected);
  const int allowed =
      std::max<int>(2, static_cast<int>(expected.size()) * 3 / 10);
  if (distance > allowed) {
    if (error) {
      *error = "text mismatch: actual='" + WideToUtf8(actual) +
               "' expected='" + WideToUtf8(expected) +
               "' distance=" + std::to_string(distance) +
               " allowed=" + std::to_string(allowed);
    }
    return false;
  }
  return true;
}

void TestAsrReplay() {
  const auto models = FindModelsDir();
  const auto wav_dir = FindWavDir();
  if (models.empty()) {
    std::printf("SKIP  asr replay: models dir not found\n");
    return;
  }
  if (wav_dir.empty()) {
    std::printf("SKIP  asr replay: fixtures dir not found\n");
    return;
  }
  const auto zh_wav = wav_dir / L"zh_greeting.wav";
  const auto en_wav = wav_dir / L"en_hello.wav";
  if (!std::filesystem::exists(zh_wav) || !std::filesystem::exists(en_wav)) {
    std::printf("SKIP  asr replay: wav fixtures missing\n");
    return;
  }

  std::string final_text;
  std::string error;
  const bool zh = ReplayAsr(models, zh_wav, "今天天气不错", &final_text, &error);
  Check(zh, "asr replay zh greeting");
  if (!zh) std::printf("      %s\n", error.c_str());

  const bool en = ReplayAsr(models, en_wav, "hello world", &final_text, &error);
  Check(en, "asr replay en hello");
  if (!en) std::printf("      %s\n", error.c_str());
}

void TestTypeOutput() {
  using namespace shuowen;
  const auto first = PlanTypeOutput("", "这是测试", true);
  Check(first.changed, "type output reports first partial");
  Check(first.next_utf8 == "這是測試", "type partial uses traditional output");
  Check(first.next == L"這是測試", "type partial wide output is traditional");

  const auto stable = PlanTypeOutput(first.next_utf8, "这是测试", true);
  Check(!stable.changed, "unchanged traditional partial is not retyped");
  Check(PostProcessText("这是", true) == L"這是", "post process traditional");
  Check(PostProcessText("这是", false) == L"这是", "post process keeps simplified");
}

void TestModelPaths() {
  const auto paths = shuowen::ModelPaths::ForTier(480);
  Check(paths.tier_ms == 480, "tier 480");
  Check(paths.Encoder().filename() == L"encoder-480ms.onnx", "encoder filename");
  Check(paths.Tokens().filename() == L"tokens.txt", "tokens filename");
  Check(shuowen::ModelPaths::NormalizeTier(123) == 960, "invalid tier defaults 960");
}

void TestModelPresence(const std::filesystem::path& dir) {
  shuowen::ModelPaths paths;
  paths.tier_ms = 480;
  paths.writable_root = dir / L"presence-writable";
  paths.bundled_root = dir / L"presence-bundled";
  std::error_code ec;
  std::filesystem::create_directories(paths.writable_root, ec);
  std::filesystem::create_directories(paths.bundled_root, ec);

  const auto make_file = [](const std::filesystem::path& file,
                            std::uintmax_t size) {
    std::error_code inner;
    std::filesystem::create_directories(file.parent_path(), inner);
    std::FILE* out = _wfopen(file.c_str(), L"wb");
    if (out) std::fclose(out);
    std::filesystem::resize_file(file, size, inner);
  };

  for (const auto& file : paths.RequiredAsrFiles()) make_file(file, 0);
  Check(!paths.AsrPresent(), "zero-byte ASR rejected");
  for (const auto& file : paths.RequiredAsrFiles()) make_file(file, 8);
  Check(paths.AsrPresent(), "non-empty ASR accepted");

  const auto vad = paths.writable_root / L"silero_vad.onnx";
  make_file(vad, 640000);
  Check(!paths.SileroPresent(), "Silero V5 rejected by size");
  make_file(vad, shuowen::ModelPaths::kMinSileroVadBytes + 1);
  Check(paths.SileroPresent(), "Silero V4 accepted by size");

  // A usable bundled VAD must win over a stale/incompatible writable copy.
  make_file(vad, 640000);
  const auto bundled_vad = paths.bundled_root / L"silero_vad.onnx";
  make_file(bundled_vad, shuowen::ModelPaths::kMinSileroVadBytes + 1);
  Check(paths.SileroVad() == bundled_vad, "bundled VAD preferred over bad writable");
}

void TestSettings(const std::filesystem::path& dir) {
  const auto file = dir / L"settings.json";
  {
    std::FILE* out = _wfopen(file.c_str(), L"wb");
    const char* json =
        R"({"Mode":"Type","Tier":"Ms480","Unknown":{"keep":1},"Vad":"Energy"})";
    std::fwrite(json, 1, std::strlen(json), out);
    std::fclose(out);
  }
  auto settings = shuowen::Settings::Load(file);
  Check(settings.mode == shuowen::DictationMode::Type, "settings mode");
  Check(settings.tier == shuowen::ModelTier::Ms480, "settings tier");
  Check(settings.vad == shuowen::VadKind::Energy, "settings vad");
  settings.hotkey_vk = 0x77;
  settings.Save(file);

  auto again = shuowen::Settings::Load(file);
  Check(again.hotkey_vk == 0x77, "settings save/load");
  Check(again.ToJson().Find("Unknown") != nullptr, "settings unknown preserved");
}

void TestHistory(const std::filesystem::path& dir) {
  const auto file = dir / L"history.json";
  std::string id;
  {
    shuowen::HistoryStore store(file);
    const auto entry = store.Append("hello 世界", "paste", false);
    id = entry.id;
    Check(!id.empty(), "history append");
    Check(store.LifetimeChars() == 8, "history lifetime chars");
    Check(store.List().size() == 1, "history list");
    store.Update(id, "hello 世界!", "", {});
    store.Flush();
  }
  {
    shuowen::HistoryStore store(file);
    Check(store.Count() == 1, "history reload");
    Check(store.List().front().text == "hello 世界!", "history update persisted");
    Check(store.Remove(id), "history delete");
    store.Clear();
    Check(store.Count() == 0, "history clear");
    Check(store.LifetimeChars() == 0, "history clear resets lifetime");
  }
}

void TestHistoryCap(const std::filesystem::path& dir) {
  const auto file = dir / L"history-cap.json";
  {
    shuowen::HistoryStore store(file);
    for (std::size_t i = 0; i < shuowen::HistoryStore::kMaxEntries + 1; ++i) {
      store.Append("x", "paste", false);
    }
    Check(store.Count() ==
              static_cast<int64_t>(shuowen::HistoryStore::kMaxEntries),
          "history caps in-memory entries");
    Check(store.LifetimeChars() ==
              static_cast<int64_t>(shuowen::HistoryStore::kMaxEntries + 1),
          "history cap keeps cumulative lifetime chars");
    store.Flush();
  }
  {
    shuowen::HistoryStore store(file);
    Check(store.Count() ==
              static_cast<int64_t>(shuowen::HistoryStore::kMaxEntries),
          "history caps persisted entries");
    Check(store.LifetimeChars() ==
              static_cast<int64_t>(shuowen::HistoryStore::kMaxEntries + 1),
          "history reload keeps cumulative lifetime chars");
  }
}

}  // namespace

int main() {
  wchar_t temp[MAX_PATH];
  DWORD length = GetTempPathW(MAX_PATH, temp);
  std::filesystem::path dir;
  if (length > 0 && length < MAX_PATH) {
    dir = std::filesystem::path(temp) /
          (L"shuowen-selftest-" + std::to_wstring(GetCurrentProcessId()));
  } else {
    dir = std::filesystem::temp_directory_path() / L"shuowen-selftest";
  }
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);

  std::printf("ShuoWen engine selftest\n");
  TestJson();
  TestText();
  TestHotkeyMatching();
  TestDictationHold();
  TestDictationThrottle();
  TestDictationPreroll();
  TestDictationCancel();
  TestDictationPause();
  TestDictationOnCall();
  TestAsrReplay();
  TestTypeOutput();
  TestModelPaths();
  TestModelPresence(dir);
  TestSettings(dir);
  TestHistory(dir);
  TestHistoryCap(dir);

  std::filesystem::remove_all(dir, ec);
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURE(S)\n", g_failures);
  return 1;
}
