# ShuoWen 测试与回归保障实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立本地一键测试体系，覆盖引擎单元、听写状态机、真实识别回放、IPC 协议与 HUD 原生窗口行为。

**Architecture:** 复用 `shuowen_selftest.exe` 的 `Check()` harness 扩展 L1–L3；为 `Dictation` 与 `EngineClient` 各引入一个最小可注入 seam；进程级行为用两个 PowerShell 脚本覆盖；`scripts\test.ps1` 串联全部层并汇总 PASS/FAIL/SKIP。

**Tech Stack:** C++20 / CMake / Ninja / MSVC，Flutter 3.x + `flutter test`，PowerShell 5.1 + Win32 P/Invoke，SAPI TTS 生成 WAV 素材。

**Spec:** `docs/superpowers/specs/2026-10-22-shuowen-test-regression-design.md`

---

## 文件结构

- `engine/src/asr/asr_backend.h`（新建）：`Dictation` 依赖的最小流式 ASR 接口。
- `engine/src/asr/sherpa_engine.h`：`SherpaEngine` 实现 `AsrBackend`。
- `engine/src/asr/dictation.h/.cpp`：构造与成员改为 `AsrBackend&`。
- `engine/src/input/hotkey.h/.cpp`：导出纯匹配函数。
- `engine/src/selftest_main.cpp`：L1/L2/L3 用例、FakeAsr、WAV 解析、资源发现。
- `engine/tests/fixtures/{zh_greeting,en_hello}.wav`（新建，提交）。
- `scripts/make_test_fixtures.ps1`（新建）。
- `scripts/_ipc_smoke.ps1`：追加 L4 用例。
- `scripts/_hud_window_smoke.ps1`（新建）：L5 窗口回归。
- `scripts/test.ps1`（新建）：一键入口。
- `ui/lib/core/ipc/plugin_client.dart`：抽象 `PipeClient`。
- `ui/lib/core/engine_client.dart`：可注入 pipe。
- `ui/test/engine_client_test.dart`（新建）。
- `README.md`、`docs/superpowers/specs/2026-10-22-shuowen-architecture-design.md`：文档更新。

统一构建命令：`cmd /c "scripts\_build_engine.bat"`；统一 selftest：`engine\build\bin\shuowen_selftest.exe`（或 `dist\shuowen_selftest.exe`，两者目录内都有 sherpa DLL）。

---

### Task 1: 抽出 AsrBackend 接口（行为不变）

**Files:**
- Create: `engine/src/asr/asr_backend.h`
- Modify: `engine/src/asr/sherpa_engine.h`
- Modify: `engine/src/asr/dictation.h`
- Modify: `engine/src/asr/dictation.cpp`

- [ ] **Step 1: 新建接口文件**

```cpp
#pragma once

#include <cstddef>
#include <string>

namespace shuowen {

// Streaming ASR surface used by Dictation. Keeping it abstract lets the
// worker loop run against a deterministic fake in the self-tests.
class AsrBackend {
 public:
  virtual ~AsrBackend() = default;

  virtual bool ready() const = 0;
  virtual void Accept(const float* samples, size_t count) = 0;
  virtual std::string Partial() = 0;
  virtual std::string Finalize() = 0;
  virtual void ResetStream() = 0;

  virtual void VadAccept(const float* samples, size_t count) = 0;
  virtual bool VadSpeech() const = 0;
  virtual bool VadHasSegment() const = 0;
  virtual void VadPopSegment() = 0;
  virtual void VadFlush() = 0;
  virtual void VadReset() = 0;
};

}  // namespace shuowen
```

- [ ] **Step 2: 让 SherpaEngine 实现接口**

在 `engine/src/asr/sherpa_engine.h` 的 include 区加入：

```cpp
#include "asr/asr_backend.h"
```

```cpp
class SherpaEngine {
```
改为：

```cpp
class SherpaEngine : public AsrBackend {
```

现有方法签名与接口一致（`ready() const`、`Partial()`、`Finalize()`、`ResetStream()`、`Vad*`），无需加 `override` 也能编译；为清晰起见在每个实现方法尾部加 `override`（`Load`/`Unload`/`Version` 除外）。

- [ ] **Step 3: Dictation 改持抽象**

`engine/src/asr/dictation.h`：

```cpp
#include "asr/sherpa_engine.h"
```
改为：

```cpp
#include "asr/asr_backend.h"
```

```cpp
  Dictation(SherpaEngine& engine, DictationMode mode);
```
改为：

```cpp
  Dictation(AsrBackend& engine, DictationMode mode);
```

```cpp
  SherpaEngine& engine_;
```
改为：

```cpp
  AsrBackend& engine_;
```

`engine/src/asr/dictation.cpp` 构造函数签名同步改为：

```cpp
Dictation::Dictation(AsrBackend& engine, DictationMode mode)
```

- [ ] **Step 4: 构建并跑现有测试**

Run: `cmd /c "scripts\_build_engine.bat"` → 构建成功。
Run: `engine\build\bin\shuowen_selftest.exe` → `ALL PASS`（44 项）。

- [ ] **Step 5: 提交**

```bash
git add engine/src/asr/asr_backend.h engine/src/asr/sherpa_engine.h engine/src/asr/dictation.h engine/src/asr/dictation.cpp
git commit -m "refactor: 抽出 AsrBackend 接口供 Dictation 测试注入"
```

---

### Task 2: 导出热键纯函数并补 L1 用例

**Files:**
- Modify: `engine/src/input/hotkey.h`
- Modify: `engine/src/input/hotkey.cpp`
- Modify: `engine/src/selftest_main.cpp`

- [ ] **Step 1: 先写失败测试**

在 `selftest_main.cpp` 的 `TestText()` 之后加入：

```cpp
void TestHotkeyMatching() {
  using namespace shuowen;
  Check(OwnModBit(VK_RCONTROL) == GlobalHotkey::kModCtrl, "own mod right ctrl");
  Check(OwnModBit(VK_LCONTROL) == GlobalHotkey::kModCtrl, "own mod left ctrl");
  Check(OwnModBit(VK_LWIN) == GlobalHotkey::kModWin, "own mod left win");
  Check(OwnModBit('A') == 0, "own mod plain key");

  Check(HotkeyMatches(VK_RCONTROL, VK_RCONTROL, GlobalHotkey::kModCtrl,
                      GlobalHotkey::kModCtrl),
        "right ctrl binding matches");
  Check(!HotkeyMatches(VK_LCONTROL, VK_RCONTROL, GlobalHotkey::kModCtrl,
                       GlobalHotkey::kModCtrl),
        "left ctrl does not match right ctrl binding");
  Check(!HotkeyMatches(VK_RCONTROL, VK_RCONTROL, GlobalHotkey::kModCtrl,
                       GlobalHotkey::kModCtrl | GlobalHotkey::kModShift),
        "extra modifier breaks match");
  Check(HotkeyMatches('A', 'A', 0, 0), "plain key binding matches");

  Check(CapturedMods(VK_RCONTROL, GlobalHotkey::kModCtrl | GlobalHotkey::kModAlt) ==
            GlobalHotkey::kModAlt,
        "capture excludes trigger modifier");
}
```

在 `main()` 的 `TestText();` 之后调用 `TestHotkeyMatching();`。

- [ ] **Step 2: 运行确认编译失败**

Run: `cmd /c "scripts\_build_engine.bat"`
Expected: 编译错误 `OwnModBit`/`HotkeyMatches`/`CapturedMods` 未声明。

- [ ] **Step 3: 导出纯函数**

`engine/src/input/hotkey.h`，在类定义之后、`HotkeyName` 之前加入：

```cpp
// Pure matching helpers shared by the keyboard hook and the self-tests.
int OwnModBit(int vk);
bool HotkeyMatches(int vk, int binding_vk, int binding_mods,
                   int current_mods);
int CapturedMods(int vk, int current_mods);
```

`engine/src/input/hotkey.cpp`：

把匿名命名空间里的 `int OwnModBit(int vk) { ... }` 整个移除，在匿名命名空间结束（`}  // namespace`）之后加入：

```cpp
int OwnModBit(int vk) {
  switch (vk) {
    case kVkLControl:
    case kVkRControl:
    case kVkControl:
      return GlobalHotkey::kModCtrl;
    case kVkLMenu:
    case kVkRMenu:
    case kVkMenu:
      return GlobalHotkey::kModAlt;
    case kVkLShift:
    case kVkRShift:
    case kVkShift:
      return GlobalHotkey::kModShift;
    case kVkLWin:
    case kVkRWin:
      return GlobalHotkey::kModWin;
    default:
      return 0;
  }
}

bool HotkeyMatches(int vk, int binding_vk, int binding_mods,
                   int current_mods) {
  if (vk != binding_vk) return false;
  const int effective = current_mods & ~OwnModBit(vk);
  return effective == binding_mods;
}

int CapturedMods(int vk, int current_mods) {
  return current_mods & ~OwnModBit(vk);
}
```

`IsModifier` 保持不变（仍在匿名命名空间内使用 `OwnModBit`）。
把 `Matches` 改为委托：

```cpp
bool GlobalHotkey::Matches(int vk) const {
  return HotkeyMatches(vk, vk_, mods_, CurrentMods());
}
```

把捕获分支改为：

```cpp
    const int captured_mods = CapturedMods(vk, CurrentMods());
```

- [ ] **Step 4: 构建并运行**

Run: `cmd /c "scripts\_build_engine.bat"` → 成功。
Run: `engine\build\bin\shuowen_selftest.exe` → `ALL PASS`，包含 8 个新 PASS。

- [ ] **Step 5: 提交**

```bash
git add engine/src/input/hotkey.h engine/src/input/hotkey.cpp engine/src/selftest_main.cpp
git commit -m "test: 覆盖热键组合匹配与触发器修饰位排除"
```

---

### Task 3: FakeAsr 与 Dictation 状态机用例（L2）

**Files:**
- Modify: `engine/src/selftest_main.cpp`

- [ ] **Step 1: 加入 FakeAsr 与等待工具**

在 `selftest_main.cpp` 的匿名命名空间内（`Check` 之后）加入：

```cpp
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "asr/dictation.h"

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
    const std::string out = final_text_;
    final_text_.clear();
    return out;
  }
  void ResetStream() override { ++reset_calls_; }
  void VadAccept(const float*, size_t count) override {
    vad_accept_calls_++;
    vad_samples_ += count;
  }
  bool VadSpeech() const override { return vad_speech_; }
  bool VadHasSegment() const override { return vad_segment_; }
  void VadPopSegment() override { vad_segment_ = false; }
  void VadFlush() override { ++vad_flush_calls_; }
  void VadReset() override {
    vad_speech_ = false;
    vad_segment_ = false;
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
};

bool WaitFor(const std::function<bool()>& pred, int timeout_ms = 3000) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return pred();
}
```

Note: 头文件 include 需要放到文件顶部；把新增 include 移到现有 include 区域。

- [ ] **Step 2: 写失败测试**

在 `TestHotkeyMatching()` 之后加入：

```cpp
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

  asr.final_text_ = "你好。";
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
  Check(WaitFor([&] { return asr.accepted_samples_.load() == 0; }),
        "frames before hold are not accepted");
  Check(WaitFor([&] { return dictation.capturing() == false; }), "idle");

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

  asr.final_text_ = "不应出现";
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
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
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
  std::vector<float> frame(480, 0.1f);

  asr.vad_speech_ = true;
  dictation.PushFrame(frame.data(), frame.size());
  Check(WaitFor([&] { return asr.accept_calls_.load() >= 1; }),
        "oncall accepts speech");

  asr.final_text_ = "第一段";
  asr.vad_speech_ = false;
  asr.vad_segment_ = true;
  dictation.PushFrame(frame.data(), frame.size());
  Check(WaitFor([&] { return finals.load() == 1; }),
        "oncall finalizes first segment");

  asr.vad_speech_ = true;
  asr.vad_segment_ = false;
  dictation.PushFrame(frame.data(), frame.size());
  asr.final_text_ = "第二段";
  asr.vad_speech_ = false;
  asr.vad_segment_ = true;
  dictation.PushFrame(frame.data(), frame.size());
  Check(WaitFor([&] { return finals.load() == 2; }),
        "oncall finalizes second segment");
  Check(texts.size() == 2 && texts[0] == "第一段" && texts[1] == "第二段",
        "oncall finals do not concatenate");
  dictation.Stop();
}
```

在 `main()` 中 `TestHotkeyMatching();` 之后加入：

```cpp
  TestDictationHold();
  TestDictationThrottle();
  TestDictationPreroll();
  TestDictationCancel();
  TestDictationPause();
  TestDictationOnCall();
```

- [ ] **Step 3: 运行确认失败**

Run: `cmd /c "scripts\_build_engine.bat"`
Expected: 编译错误（`AsrBackend` 尚未被 `Dictation` 接受或 FakeAsr 签名不匹配）。若 Task 1 已完成，则本步应直接编译通过；跳过失败确认，直接进入 Step 4。

- [ ] **Step 4: 运行全部用例**

Run: `engine\build\bin\shuowen_selftest.exe`
Expected: `ALL PASS`，新增 15 项左右 PASS。若 `partial throttled inside 150ms` 偶发失败，检查是否机器卡顿导致处理超过 150 ms；将该断言的等待前置 `WaitFor(accept_calls>=5)` 已保证帧已处理。

- [ ] **Step 5: 提交**

```bash
git add engine/src/selftest_main.cpp
git commit -m "test: 用 FakeAsr 覆盖听写状态机、节流、preroll、取消与暂停"
```

---

### Task 4: WAV 素材、资源发现与真实识别回放（L3）

**Files:**
- Create: `scripts/make_test_fixtures.ps1`
- Create: `engine/tests/fixtures/zh_greeting.wav`
- Create: `engine/tests/fixtures/en_hello.wav`
- Modify: `engine/src/selftest_main.cpp`

- [ ] **Step 1: 写素材生成脚本**

`scripts/make_test_fixtures.ps1`：

```powershell
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "engine\tests\fixtures")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Speech
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function New-Fixture {
    param([string]$Text, [string]$Voice, [string]$FileName)
    $path = Join-Path $OutDir $FileName
    $synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
    try {
        $synth.SelectVoice($Voice)
        $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(
            16000,
            [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
            [System.Speech.AudioFormat.AudioChannel]::Mono)
        $synth.SetOutputToWaveFile($path, $format)
        $synth.Speak($Text)
    } finally {
        $synth.Dispose()
    }
    $size = (Get-Item $path).Length
    if ($size -lt 10000) { throw "fixture too small: $path ($size bytes)" }
    Write-Output "wrote $FileName ($size bytes)"
}

New-Fixture -Text "今天天气不错" -Voice "Microsoft Huihui Desktop" -FileName "zh_greeting.wav"
New-Fixture -Text "hello world" -Voice "Microsoft Zira Desktop" -FileName "en_hello.wav"
```

- [ ] **Step 2: 以 UTF-8 BOM 保存后生成素材**

Run:

```powershell
powershell -NoProfile -Command "$p='D:\workspace\ShuoWen\scripts\make_test_fixtures.ps1'; $t=[IO.File]::ReadAllText($p,[Text.UTF8Encoding]::new($false)); [IO.File]::WriteAllText($p,$t,[Text.UTF8Encoding]::new($true))"
powershell -ExecutionPolicy Bypass -File scripts\make_test_fixtures.ps1
```

Expected: 两行 `wrote ... (xxxxx bytes)`，文件位于 `engine\tests\fixtures\`。用 `Get-Content` 检查 WAV 头为 `RIFF...WAVE`（可选）。

- [ ] **Step 3: 写失败测试：WAV 解析 + 资源发现 + 回放**

在 `selftest_main.cpp` 顶部 include 区加入：

```cpp
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>

#include "asr/asr_backend.h"
#include "asr/sherpa_engine.h"
#include "util/text.h"
```

在 `WaitFor` 之后加入：

```cpp
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
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) |
           (static_cast<uint32_t>(b[3]) << 24);
  };
  auto read_u16 = [&]() {
    unsigned char b[2];
    in.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
  };
  char riff[4], wave[4];
  in.read(riff, 4);
  (void)read_u32();
  in.read(wave, 4);
  if (!in || std::memcmp(riff, "RIFF", 4) != 0 ||
      std::memcmp(wave, "WAVE", 4) != 0) {
    if (error) *error = "not a wav file";
    return false;
  }
  int channels = 0, bits = 0, rate = 0;
  while (in) {
    char id[4];
    if (!in.read(id, 4)) break;
    const uint32_t size = read_u32();
    if (std::memcmp(id, "fmt ", 4) == 0) {
      const uint16_t format = read_u16();
      channels = read_u16();
      rate = static_cast<int>(read_u32());
      (void)read_u32();
      bits = read_u16();
      if (format != 1 || channels != 1 || bits != 16) {
        if (error) *error = "expect 16-bit mono pcm";
        return false;
      }
      in.seekg(static_cast<std::streamoff>(size) - 16, std::ios::cur);
    } else if (std::memcmp(id, "data", 4) == 0) {
      std::vector<int16_t> pcm(size / 2);
      in.read(reinterpret_cast<char*>(pcm.data()),
              static_cast<std::streamsize>(pcm.size() * 2));
      out->samples.resize(pcm.size());
      for (size_t i = 0; i < pcm.size(); ++i) {
        out->samples[i] = static_cast<float>(pcm[i]) / 32768.0f;
      }
      out->sample_rate = rate;
      return out->sample_rate == shuowen::kSampleRate && !out->samples.empty();
    } else {
      in.seekg(static_cast<std::streamoff>(size + (size & 1)), std::ios::cur);
    }
  }
  if (error) *error = "wav data chunk missing";
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
      exe / L".." / L".." / L".." / L"tests" / L"fixtures"};
  for (const auto& candidate : candidates) {
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) return candidate;
  }
  return {};
}

std::wstring StripForCompare(std::wstring text) {
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

bool ReplayAsr(const std::filesystem::path& models, const std::filesystem::path& wav,
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
  if (!engine.Load(paths, VadKind::Silero, &load_error) &&
      !engine.Load(paths, VadKind::Energy, &load_error)) {
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

  const std::wstring actual = StripForCompare(shuowen::Utf8ToWide(text));
  const std::wstring expected = StripForCompare(shuowen::Utf8ToWide(expected_utf8));
  const int distance = EditDistance(actual, expected);
  const int allowed = std::max<int>(2, static_cast<int>(expected.size()) * 3 / 10);
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
```

在 `main()` 的 `TestDictationOnCall();` 之后加入 `TestAsrReplay();`。

- [ ] **Step 4: 构建并运行**

Run: `cmd /c "scripts\_build_engine.bat"` → 成功。
Run:

```powershell
$env:SHUOWEN_MODELS_DIR="D:\workspace\ShuoWen\dist\models"
$env:SHUOWEN_TEST_WAV_DIR="D:\workspace\ShuoWen\engine\tests\fixtures"
D:\workspace\ShuoWen\engine\build\bin\shuowen_selftest.exe
```

Expected: `PASS  asr replay zh greeting`、`PASS  asr replay en hello`、`ALL PASS`。
若中文识别文本差异超阈值，先打印实际文本（失败时已打印），再决定放宽阈值或改素材文本；不要为过测试而放宽到失去意义（上限 40%）。

- [ ] **Step 5: 提交**

```bash
git add scripts/make_test_fixtures.ps1 engine/tests/fixtures/zh_greeting.wav engine/tests/fixtures/en_hello.wav engine/src/selftest_main.cpp
git commit -m "test: 增加 TTS 语音素材与真实模型回放用例"
```

---

### Task 5: 扩展 IPC 冒烟（L4）

**Files:**
- Modify: `scripts/_ipc_smoke.ps1`

- [ ] **Step 1: 在 `Read-Response` 之后加入收集事件的版本**

```powershell
function Read-ResponseCollect {
    param($Reader, [int]$Id)
    $events = @()
    while ($true) {
        $line = $Reader.ReadLine()
        if ($null -eq $line) { throw "pipe closed while waiting for response id=$Id" }
        $obj = $line | ConvertFrom-Json
        if ($obj.PSObject.Properties.Name -contains "id" -and $obj.id -eq $Id) {
            return @{ response = $obj; events = $events }
        }
        if ($obj.PSObject.Properties.Name -contains "event") { $events += $obj.event }
    }
}
```

- [ ] **Step 2: 在 `settings.get`（id=3）之后追加 L4 用例**

```powershell
    $writer.WriteLine('{"id":10,"cmd":"nope"}')
    $bad = Read-Response -Reader $reader -Id 10
    if ($bad.ok) { throw "unknown command should fail" }
    if (-not $bad.error.code) { throw "unknown command should carry error.code" }
    Write-Output ("unknownCommand.code=" + $bad.error.code)

    $writer.WriteLine('{"id":11,"cmd":"settings.patch","args":{"patch":[1,2]}}')
    $badPatch = Read-Response -Reader $reader -Id 11
    if ($badPatch.ok) { throw "array patch should fail" }
    Write-Output ("badPatch.code=" + $badPatch.error.code)

    $originalStay = [double]$settings.data.HudStaySeconds
    $writer.WriteLine('{"id":12,"cmd":"settings.patch","args":{"patch":{"HudStaySeconds":0.75}}}')
    $collected = Read-ResponseCollect -Reader $reader -Id 12
    if (-not $collected.response.ok) { throw "settings.patch failed" }
    if ($collected.events -notcontains "settings") {
        throw "settings.patch did not broadcast a settings event"
    }
    Write-Output ("settings.patch.broadcast=" + ($collected.events -join ","))

    $writer.WriteLine('{"id":13,"cmd":"settings.get"}')
    $after = Read-Response -Reader $reader -Id 13
    if ([double]$after.data.HudStaySeconds -ne 0.75) {
        throw "settings.patch did not stick"
    }
    Write-Output "settings.patch.roundtrip=ok"

    $writer.WriteLine(('{"id":14,"cmd":"settings.patch","args":{"patch":{"HudStaySeconds":' + $originalStay + '}}}'))
    $restored = Read-Response -Reader $reader -Id 14
    if (-not $restored.ok) { throw "restore failed" }
    Write-Output "settings.patch.restore=ok"

    $writer.WriteLine('{"id":15,"cmd":"history.list","args":{"limit":5}}')
    $hist = Read-Response -Reader $reader -Id 15
    if (-not $hist.ok) { throw "history.list failed" }
    if ($null -eq $hist.data.lifetimeChars) { throw "history lifetimeChars missing" }
    Write-Output ("history.entries=" + @($hist.data.entries).Count)
```

- [ ] **Step 3: 运行**

Run: `powershell -ExecutionPolicy Bypass -File scripts\_ipc_smoke.ps1`
Expected: 原有字段输出 + `unknownCommand.code=unknown_command` + `settings.patch.roundtrip=ok` + `history.entries=N`，退出码 0。脚本会复用正在运行的引擎，不会关闭它。

- [ ] **Step 4: 提交**

```bash
git add scripts/_ipc_smoke.ps1
git commit -m "test: 扩展 IPC 冒烟覆盖错误码、设置往返与广播事件"
```

---

### Task 6: HUD 原生窗口回归（L5）

**Files:**
- Create: `scripts/_hud_window_smoke.ps1`

- [ ] **Step 1: 写脚本**

```powershell
[CmdletBinding()]
param(
    [string]$EngineExe = "",
    [string]$UiExe = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $EngineExe) { $EngineExe = Join-Path $root "dist\shuowen_engine.exe" }
if (-not $UiExe) { $UiExe = Join-Path $root "dist\shuowen_ui.exe" }

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class SwWin {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextLength(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hgt, uint flags);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool GetLayeredWindowAttributes(IntPtr h, out uint key, out byte alpha, out uint flags);
  [DllImport("user32.dll")] public static extern int GetWindowRgnBox(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@

function Find-HudWindow {
    $script:hud = [IntPtr]::Zero
    $script:hudPid = 0
    $cb = [SwWin+EnumProc]{
        param($h, $l)
        $len = [SwWin]::GetWindowTextLength($h)
        if ($len -gt 0) {
            $sb = New-Object System.Text.StringBuilder ($len + 1)
            [void][SwWin]::GetWindowText($h, $sb, $sb.Capacity)
            if ($sb.ToString() -eq "ShuoWen HUD") { $script:hud = $h }
        }
        return $true
    }
    [void][SwWin]::EnumWindows($cb, [IntPtr]::Zero)
    return $script:hud
}

$engine = Get-Process shuowen_engine -ErrorAction SilentlyContinue | Select-Object -First 1
$startedEngine = $null
if (-not $engine) {
    if (-not (Test-Path $EngineExe) -or -not (Test-Path $UiExe)) {
        throw "dist binaries missing; run scripts\package.ps1 first"
    }
    $startedEngine = Start-Process -FilePath $EngineExe -WorkingDirectory (Split-Path -Parent $EngineExe) -PassThru -WindowStyle Hidden
}

$hud = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds(15)
while ((Get-Date) -lt $deadline) {
    $hud = Find-HudWindow
    if ($hud -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 250
}
if ($hud -eq [IntPtr]::Zero) { throw "HUD window not found within 15s" }

$hudPid = 0
[void][SwWin]::GetWindowThreadProcessId($hud, [ref]$hudPid)

$failures = 0
function Assert([bool]$condition, [string]$name) {
    if ($condition) { Write-Output "PASS  $name" }
    else { Write-Output "FAIL  $name"; $script:failures++ }
}

$ex = [SwWin]::GetWindowLong($hud, -20)
Assert (($ex -band 0x80000) -ne 0) "hud has WS_EX_LAYERED"
Assert (($ex -band 0x20) -ne 0) "hud has WS_EX_TRANSPARENT"
Assert (($ex -band 0x08000000) -ne 0) "hud has WS_EX_NOACTIVATE"

$key = 0; $alpha = 0; $flags = 0
$ok = [SwWin]::GetLayeredWindowAttributes($hud, [ref]$key, [ref]$alpha, [ref]$flags)
Assert ($ok -and $alpha -eq 255 -and $flags -eq 2) "hud layered attributes are opaque (black-window regression)"

$rect = New-Object SwWin+RECT
[void][SwWin]::GetWindowRect($hud, [ref]$rect)
$dpi = [SwWin]::GetDpiForWindow($hud)
$scale = $dpi / 96.0
$width = $rect.R - $rect.L
$height = $rect.B - $rect.T
Assert ([Math]::Abs($width - 500 * $scale) -le 2) "hud width is 500 logical px (got $width @ ${scale}x)"
Assert ([Math]::Abs($height - 104 * $scale) -le 2) "hud height is 104 logical px (got $height @ ${scale}x)"

$rgnBox = New-Object SwWin+RECT
$rgnType = [SwWin]::GetWindowRgnBox($hud, [ref]$rgnBox)
Assert ($rgnType -ne 0) "hud has a rounded window region (type=$rgnType)"

[void][SwWin]::SetWindowPos($hud, [IntPtr](-1), $rect.L, $rect.T, 0, 0, 0x0001 -bor 0x0002 -bor 0x0010 -bor 0x0040)
Start-Sleep -Seconds 2

Add-Type -AssemblyName System.Drawing
$shot = New-Object System.Drawing.Bitmap($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($shot)
$graphics.CopyFromScreen($rect.L, $rect.T, 0, 0, $shot.Size)
$graphics.Dispose()
$accent = 0
for ($x = 0; $x -lt $width; $x += 2) {
    for ($y = 0; $y -lt $height; $y += 2) {
        $c = $shot.GetPixel($x, $y)
        $a1 = [Math]::Abs($c.R - 0x7C) -le 40 -and [Math]::Abs($c.G - 0x5C) -le 40 -and [Math]::Abs($c.B - 0xFF) -le 40
        $a2 = [Math]::Abs($c.R - 0x38) -le 40 -and [Math]::Abs($c.G - 0xE1) -le 40 -and [Math]::Abs($c.B - 0xD6) -le 40
        if ($a1 -or $a2) { $accent++ }
    }
}
$shotPath = Join-Path $env:TEMP "shuowen_hud_smoke.png"
$shot.Save($shotPath)
$shot.Dispose()
Assert ($accent -gt 50) "hud renders accent pixels (found $accent, screenshot $shotPath)"

[void][SwWin]::ShowWindow($hud, 0)

if ($startedEngine) {
    Stop-Process -Id $startedEngine.Id -Force -ErrorAction SilentlyContinue
    Stop-Process -Id $hudPid -Force -ErrorAction SilentlyContinue
}

if ($failures -gt 0) { throw "$failures HUD window assertion(s) failed" }
Write-Output "HUD window smoke passed"
```

- [ ] **Step 2: 运行**

Run: `powershell -ExecutionPolicy Bypass -File scripts\_hud_window_smoke.ps1`
Expected: 6 个 `PASS` + `HUD window smoke passed`。已有引擎运行时复用其 HUD，只隐藏窗口。

- [ ] **Step 3: 提交**

```bash
git add scripts/_hud_window_smoke.ps1
git commit -m "test: 增加 HUD 原生窗口回归（层属性/尺寸/圆角/渲染）"
```

---

### Task 7: EngineClient 可注入 + UI 协议测试（L6）

**Files:**
- Modify: `ui/lib/core/ipc/plugin_client.dart`
- Modify: `ui/lib/core/engine_client.dart`
- Create: `ui/test/engine_client_test.dart`

- [ ] **Step 1: 抽象 PipeClient**

`plugin_client.dart` 顶部加入：

```dart
/// Seam for EngineClient tests. The production implementation wraps the
/// native named-pipe client in the Windows runner.
abstract class PipeClient {
  Stream<String> get lines;
  Future<bool> connect({String pipeName});
  Future<bool> send(String line);
  Future<void> dispose();
}
```

把 `class PluginPipeClient {` 改为 `class PluginPipeClient implements PipeClient {`。

- [ ] **Step 2: EngineClient 注入**

`engine_client.dart`：

```dart
  EngineClient(this.windowType);
```
改为：

```dart
  EngineClient(this.windowType, {PipeClient? pipe}) : _pipe = pipe;
```

```dart
  PluginPipeClient? _pipe;
```
改为：

```dart
  PipeClient? _pipe;
```

`start()` 中的 `_pipe ??= PluginPipeClient();` 保持不变。

- [ ] **Step 3: 写测试**

`ui/test/engine_client_test.dart`：

```dart
import 'dart:async';
import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:shuowen_ui/core/engine_client.dart';
import 'package:shuowen_ui/core/ipc/plugin_client.dart';

class FakePipe implements PipeClient {
  final StreamController<String> _lines = StreamController<String>.broadcast();
  final List<Map<String, dynamic>> sent = [];
  Map<String, dynamic>? Function(String cmd)? responder;

  @override
  Stream<String> get lines => _lines.stream;

  @override
  Future<bool> connect({String pipeName = r'\\.\pipe\shuowen.engine'}) async => true;

  @override
  Future<bool> send(String line) async {
    final map = jsonDecode(line) as Map<String, dynamic>;
    sent.add(map);
    final response = responder?.call(map['cmd'] as String);
    if (response != null) {
      scheduleMicrotask(() => _lines.add(jsonEncode(response)));
    }
    return true;
  }

  @override
  Future<void> dispose() async {
    await _lines.close();
  }

  void emit(String line) => _lines.add(line);
}

void main() {
  test('hello snapshot populates state', () async {
    final pipe = FakePipe();
    pipe.responder = (cmd) => {
          'id': 1,
          'ok': true,
          'data': {
            'settings': {'Mode': 'Paste', 'Tier': 960},
            'engine': {'status': 'ready', 'hotkeyName': 'RightCtrl'},
            'models': {'tiers': []},
            'hud': {'visible': false, 'state': 'idle'},
            'oncall': {'segments': []},
            'history': {'lifetimeChars': 42, 'count': 3, 'recent': []},
          },
        };
    final client = EngineClient('settings', pipe: pipe);
    await client.start();
    expect(client.connected, isTrue);
    expect(client.settings['Mode'], 'Paste');
    expect(client.engineStatus, 'ready');
    expect(client.lifetimeChars, 42);
    expect(client.totalEntries, 3);
    client.dispose();
  });

  test('events update state and notifiers', () async {
    final pipe = FakePipe();
    final client = EngineClient('hud', pipe: pipe);
    await client.start();
    pipe.emit(jsonEncode({
      'event': 'partial',
      'data': {'text': '你好', 'level': 0.5},
    }));
    await Future<void>.delayed(Duration.zero);
    expect(client.partial, '你好');
    expect(client.levelNotifier.value, closeTo(0.5, 0.001));

    pipe.emit(jsonEncode({'event': 'toast', 'data': {'kind': 'error', 'message': 'x'}}));
    await Future<void>.delayed(Duration.zero);
    expect(client.toastSerial, 1);
    client.dispose();
  });

  test('error responses surface as EngineException', () async {
    final pipe = FakePipe();
    pipe.responder = (cmd) => {
          'id': 1,
          'ok': true,
          'data': {
            'settings': {},
            'engine': {},
            'models': {},
            'hud': {},
            'oncall': {},
            'history': {},
          },
        };
    final client = EngineClient('settings', pipe: pipe);
    await client.start();
    pipe.responder = (cmd) => {
          'id': 2,
          'ok': false,
          'error': {'code': 'bad_args', 'message': 'nope'},
        };
    await expectLater(
      client.request('settings.patch', {'patch': 1}),
      throwsA(isA<EngineException>()
          .having((e) => e.code, 'code', 'bad_args')),
    );
    client.dispose();
  });

  test('closed pipe marks the client disconnected', () async {
    final pipe = FakePipe();
    final client = EngineClient('hud', pipe: pipe);
    await client.start();
    pipe.emit('__shuowen_closed__');
    await Future<void>.delayed(Duration.zero);
    expect(client.connected, isFalse);
    expect(client.connectionError, isNotEmpty);
    client.dispose();
  });
}
```

注意：`EngineClient.start()` 内部有 100 ms 延迟与 `request('hello')` 5 秒超时；FakePipe 必须在 `send('hello')` 时回包，上面的 responder 已处理。每个用例结束调用 `dispose()` 取消重连计时器。

- [ ] **Step 4: 运行**

Run: `cd ui; flutter analyze` → `No issues found!`
Run: `cd ui; flutter test` → 现有 5 项 + 新增 4 项全部通过。

- [ ] **Step 5: 提交**

```bash
git add ui/lib/core/ipc/plugin_client.dart ui/lib/core/engine_client.dart ui/test/engine_client_test.dart
git commit -m "test: 覆盖 EngineClient 快照、事件、错误与断线状态"
```

---

### Task 8: 一键入口 test.ps1 与文档

**Files:**
- Create: `scripts/test.ps1`
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-10-22-shuowen-architecture-design.md`

- [ ] **Step 1: 写 test.ps1**

```powershell
[CmdletBinding()]
param(
    [string[]]$Only = @("engine", "ui", "ipc", "window"),
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$results = [ordered]@{}
$failed = $false

function Want([string]$name) { return $Only -contains $name }

if ($Build) {
    Write-Host "== build (package.ps1) =="
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "package.ps1")
    if ($LASTEXITCODE -ne 0) { throw "package.ps1 failed ($LASTEXITCODE)" }
}

if (Want "engine") {
    Write-Host "== engine: selftest =="
    $selftest = Join-Path $root "dist\shuowen_selftest.exe"
    if (-not (Test-Path $selftest)) {
        $selftest = Join-Path $root "engine\build\bin\shuowen_selftest.exe"
    }
    if (-not (Test-Path $selftest)) {
        $results.engine = "SKIP (selftest missing; run with -Build)"
    } else {
        $env:SHUOWEN_MODELS_DIR = Join-Path $root "dist\models"
        $env:SHUOWEN_TEST_WAV_DIR = Join-Path $root "engine\tests\fixtures"
        & $selftest
        $code = $LASTEXITCODE
        $results.engine = if ($code -eq 0) { "PASS" } else { "FAIL" }
        if ($code -ne 0) { $failed = $true }
    }
}

if (Want "ui") {
    Write-Host "== ui: analyze + test =="
    Push-Location (Join-Path $root "ui")
    try {
        & flutter analyze
        $analyze = $LASTEXITCODE
        & flutter test
        $test = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($analyze -eq 0 -and $test -eq 0) { $results.ui = "PASS" }
    else { $results.ui = "FAIL"; $failed = $true }
}

if (Want "ipc") {
    Write-Host "== ipc: smoke =="
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "_ipc_smoke.ps1")
    $code = $LASTEXITCODE
    $results.ipc = if ($code -eq 0) { "PASS" } else { "FAIL" }
    if ($code -ne 0) { $failed = $true }
}

if (Want "window") {
    Write-Host "== window: hud smoke =="
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "_hud_window_smoke.ps1")
    $code = $LASTEXITCODE
    $results.window = if ($code -eq 0) { "PASS" } else { "FAIL" }
    if ($code -ne 0) { $failed = $true }
}

Write-Host ""
Write-Host "== summary =="
foreach ($key in $results.Keys) { Write-Host ("{0,-8} {1}" -f $key, $results[$key]) }
if ($failed) { exit 1 }
exit 0
```

- [ ] **Step 2: 全量运行**

Run: `powershell -ExecutionPolicy Bypass -File scripts\test.ps1`
Expected: engine PASS、ui PASS、ipc PASS、window PASS，退出码 0，总时长 < 60 秒（引擎 selftest 含两次真实识别回放约 6–10 秒）。

- [ ] **Step 3: 更新 README 验证章节**

在 `## 验证` 的代码块中加入：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test.ps1
```

并在其下补充一句：

```markdown
`scripts\test.ps1` 依次运行引擎自测（含真实模型 WAV 回放，缺模型/素材时自动 SKIP）、`flutter analyze`/`flutter test`、IPC 冒烟与 HUD 窗口回归；`-Only engine,ui,ipc,window` 可选层，`-Build` 先完整打包。
```

- [ ] **Step 4: 更新架构文档 13.2 覆盖缺口**

把「听写热路径没有自动化用例」与「UI 侧只有 5 个单测」两条末尾追加：

```markdown
  - 2026-10-22 已补：`engine/src/selftest_main.cpp` 的 Dictation 状态机用例（FakeAsr）与真实模型 WAV 回放（`engine/tests/fixtures`）；`scripts/_ipc_smoke.ps1` 覆盖 IPC 错误码/设置往返/广播；`scripts/_hud_window_smoke.ps1` 覆盖 HUD 层属性与渲染；`ui/test/engine_client_test.dart` 覆盖客户端快照/事件/错误/断线。入口 `scripts\test.ps1`。
```

- [ ] **Step 5: 提交**

```bash
git add scripts/test.ps1 README.md docs/superpowers/specs/2026-10-22-shuowen-architecture-design.md
git commit -m "test: 新增一键测试入口并更新文档"
```

---

## 验证清单

- [ ] `cmd /c "scripts\_build_engine.bat"` 成功。
- [ ] `engine\build\bin\shuowen_selftest.exe` 全 PASS（含两条真实识别回放）。
- [ ] `cd ui; flutter analyze` 无问题；`flutter test` 9 项全过。
- [ ] `powershell -File scripts\_ipc_smoke.ps1` 退出码 0。
- [ ] `powershell -File scripts\_hud_window_smoke.ps1` 退出码 0。
- [ ] `powershell -File scripts\test.ps1` 汇总 4 层 PASS，退出码 0。
- [ ] `git status` 干净（临时截图只写 `%TEMP%`）。