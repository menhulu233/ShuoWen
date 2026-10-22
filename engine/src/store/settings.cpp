#include "settings.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "util/log.h"
#include "util/text.h"

namespace shuowen {
namespace {

std::string ReadFileUtf8(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return {};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool WriteFileUtf8Atomic(const std::filesystem::path& file,
                         const std::string& text) {
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  const auto tmp = file.wstring() + L".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) return false;
  }
  if (!MoveFileExW(tmp.c_str(), file.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

int ClampInt(int value, int low, int high) {
  return std::max(low, std::min(high, value));
}

double ClampDouble(double value, double low, double high) {
  return std::max(low, std::min(high, value));
}

}  // namespace

std::string ModeToHistoryCode(DictationMode mode) {
  switch (mode) {
    case DictationMode::Paste: return "paste";
    case DictationMode::Type: return "type";
    case DictationMode::OnCall: return "oncall";
  }
  return "paste";
}

std::string ModeToJsonProperty(DictationMode mode) {
  switch (mode) {
    case DictationMode::Paste: return "Paste";
    case DictationMode::Type: return "Type";
    case DictationMode::OnCall: return "OnCall";
  }
  return "Paste";
}

DictationMode Settings::ParseMode(const std::string& value) {
  if (value == "Type" || value == "type") return DictationMode::Type;
  if (value == "OnCall" || value == "oncall") return DictationMode::OnCall;
  return DictationMode::Paste;
}

TriggerMode Settings::ParseTrigger(const std::string& value) {
  if (value == "Latch" || value == "latch") return TriggerMode::Latch;
  if (value == "Toggle" || value == "toggle") return TriggerMode::Toggle;
  return TriggerMode::Hold;
}

VadKind Settings::ParseVad(const std::string& value) {
  if (value == "Energy" || value == "energy") return VadKind::Energy;
  return VadKind::Silero;
}

ModelTier Settings::ParseTier(const std::string& value) {
  const int ms = TierFromCode(value);
  switch (ms) {
    case 160: return ModelTier::Ms160;
    case 480: return ModelTier::Ms480;
    case 1920: return ModelTier::Ms1920;
    default: return ModelTier::Ms960;
  }
}

int Settings::TierFromCode(const std::string& value) {
  if (value == "Ms160" || value == "160") return 160;
  if (value == "Ms480" || value == "480") return 480;
  if (value == "Ms1920" || value == "1920") return 1920;
  return 960;
}

std::string Settings::ModeCode() const { return ModeToJsonProperty(mode); }
std::string Settings::HistoryModeCode() const { return ModeToHistoryCode(mode); }

std::string Settings::TriggerCode() const {
  switch (trigger) {
    case TriggerMode::Hold: return "Hold";
    case TriggerMode::Latch: return "Latch";
    case TriggerMode::Toggle: return "Toggle";
  }
  return "Hold";
}

std::string Settings::VadCode() const {
  return vad == VadKind::Energy ? "Energy" : "Silero";
}

std::string Settings::TierCode() const {
  return "Ms" + std::to_string(TierMs());
}

Settings Settings::Load(const std::filesystem::path& file) {
  Settings settings;
  const std::string text = ReadFileUtf8(file);
  if (text.empty()) return settings;
  std::string error;
  Json root = Json::Parse(text, &error);
  if (!root.is_object()) {
    LogF("settings: parse failed (%s), using defaults", error.c_str());
    return settings;
  }
  settings.unknown_ = root;
  settings.ApplyJson(root);
  return settings;
}

void Settings::ApplyJson(const Json& patch) {
  if (!patch.is_object()) return;
  if (const Json* v = patch.Find("Mode"); v && v->is_string()) mode = ParseMode(v->AsString());
  if (const Json* v = patch.Find("Tier"); v && v->is_string()) tier = ParseTier(v->AsString());
  if (const Json* v = patch.Find("ModelSource"); v && v->is_string()) model_source = v->AsString();
  if (const Json* v = patch.Find("Vad"); v && v->is_string()) vad = ParseVad(v->AsString());
  if (const Json* v = patch.Find("HotkeyVk"); v && v->is_number()) hotkey_vk = ClampInt(v->AsInt(0xA3), 1, 255);
  if (const Json* v = patch.Find("HotkeyMods"); v && v->is_number()) hotkey_mods = ClampInt(v->AsInt(0), 0, 15);
  if (const Json* v = patch.Find("Trigger"); v && v->is_string()) trigger = ParseTrigger(v->AsString());
  if (const Json* v = patch.Find("OnCallAutoStart"); v && v->is_bool()) oncall_auto_start = v->AsBool();
  if (const Json* v = patch.Find("HudStaySeconds"); v && v->is_number()) hud_stay_seconds = ClampDouble(v->AsDouble(0.5), 0.0, 10.0);
  if (const Json* v = patch.Find("MicMuted"); v && v->is_bool()) mic_muted = v->AsBool();
  if (const Json* v = patch.Find("Language"); v && v->is_string()) language = v->AsString();
  if (const Json* v = patch.Find("ClipboardOverwrite"); v && v->is_bool()) clipboard_overwrite = v->AsBool();
  if (const Json* v = patch.Find("HistoryEnabled"); v && v->is_bool()) history_enabled = v->AsBool();
  if (const Json* v = patch.Find("LaunchAtLogin"); v && v->is_bool()) launch_at_login = v->AsBool();
  if (const Json* v = patch.Find("Welcomed"); v && v->is_bool()) welcomed = v->AsBool();
  if (const Json* v = patch.Find("MicDeviceId"); v && v->is_string()) mic_device_id = v->AsString();
  if (const Json* v = patch.Find("CueEnabled"); v && v->is_bool()) cue_enabled = v->AsBool();
  if (const Json* v = patch.Find("CueTheme"); v && v->is_string()) cue_theme = v->AsString();
  if (const Json* v = patch.Find("CueVolume"); v && v->is_string()) cue_volume = v->AsString();
  if (const Json* v = patch.Find("OutputTraditional"); v && v->is_bool()) output_traditional = v->AsBool();

  if (const Json* v = patch.Find("Mode"); v && v->is_string()) {
    unknown_["Mode"] = *v;
  }
}

Json Settings::ToJson() const {
  Json root = unknown_.is_object() ? unknown_ : Json::Object();
  root.Set("Mode", Json(ModeCode()));
  root.Set("Tier", Json(TierCode()));
  root.Set("ModelSource", Json(model_source));
  root.Set("Vad", Json(VadCode()));
  root.Set("HotkeyVk", Json(hotkey_vk));
  root.Set("HotkeyMods", Json(hotkey_mods));
  root.Set("Trigger", Json(TriggerCode()));
  root.Set("OnCallAutoStart", Json(oncall_auto_start));
  root.Set("HudStaySeconds", Json(hud_stay_seconds));
  root.Set("MicMuted", Json(mic_muted));
  root.Set("Language", Json(language));
  root.Set("ClipboardOverwrite", Json(clipboard_overwrite));
  root.Set("HistoryEnabled", Json(history_enabled));
  root.Set("LaunchAtLogin", Json(launch_at_login));
  root.Set("Welcomed", Json(welcomed));
  root.Set("MicDeviceId", Json(mic_device_id));
  root.Set("CueEnabled", Json(cue_enabled));
  root.Set("CueTheme", Json(cue_theme));
  root.Set("CueVolume", Json(cue_volume));
  root.Set("OutputTraditional", Json(output_traditional));
  return root;
}

void Settings::Save(const std::filesystem::path& file) const {
  if (!WriteFileUtf8Atomic(file, ToJson().Dump(2) + "\n")) {
    LogF("settings: failed to save %s", WideToUtf8(file.wstring()).c_str());
  }
}

}  // namespace shuowen
