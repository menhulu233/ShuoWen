#pragma once

#include <filesystem>
#include <string>

#include "util/json.h"

namespace shuowen {

enum class DictationMode { Paste, Type, OnCall };
enum class TriggerMode { Hold, Latch, Toggle };
enum class VadKind { Silero, Energy };
enum class ModelTier { Ms160 = 160, Ms480 = 480, Ms960 = 960, Ms1920 = 1920 };

struct Settings {
  DictationMode mode = DictationMode::Paste;
  ModelTier tier = ModelTier::Ms960;
  std::string model_source = "official";
  VadKind vad = VadKind::Silero;
  int hotkey_vk = 0xA3;  // VK_RCONTROL
  int hotkey_mods = 0;
  TriggerMode trigger = TriggerMode::Hold;
  bool oncall_auto_start = false;
  double hud_stay_seconds = 0.5;
  bool mic_muted = false;
  std::string language = "auto";
  bool clipboard_overwrite = false;
  bool history_enabled = true;
  bool launch_at_login = true;
  bool welcomed = false;
  std::string mic_device_id;
  bool cue_enabled = true;
  std::string cue_theme = "chime";
  std::string cue_volume = "low";
  bool output_traditional = false;

  static Settings Load(const std::filesystem::path& file);
  void Save(const std::filesystem::path& file) const;

  Json ToJson() const;
  // Applies recognized keys from patch; unknown keys are kept for the next save.
  void ApplyJson(const Json& patch);

  std::string ModeCode() const;
  std::string TriggerCode() const;
  std::string VadCode() const;
  std::string TierCode() const;
  std::string HistoryModeCode() const;
  int TierMs() const { return static_cast<int>(tier); }

  static DictationMode ParseMode(const std::string& value);
  static TriggerMode ParseTrigger(const std::string& value);
  static VadKind ParseVad(const std::string& value);
  static ModelTier ParseTier(const std::string& value);
  static int TierFromCode(const std::string& value);

 private:
  Json unknown_ = Json::Object();
};

std::string ModeToHistoryCode(DictationMode mode);
std::string ModeToJsonProperty(DictationMode mode);

}  // namespace shuowen
