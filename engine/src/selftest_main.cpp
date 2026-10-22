#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

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
