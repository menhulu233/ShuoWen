#pragma once

#include <windows.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "asr/dictation.h"
#include "asr/sherpa_engine.h"
#include "audio/wasapi.h"
#include "input/hotkey.h"
#include "ipc/pipe_server.h"
#include "models/download.h"
#include "models/paths.h"
#include "store/history.h"
#include "store/settings.h"
#include "ui/launcher.h"
#include "ui/tray.h"

namespace shuowen {

class App {
 public:
  App() = default;
  ~App();

  bool Initialize(HINSTANCE instance);
  int Run();
  void Quit();
  void Shutdown();

 private:
  std::string HandleLine(uint64_t client_id, const std::string& line);

  Settings CopySettings() const;
  std::string SettingsJson() const;
  Json StateJson() const;
  Json ModelsJson() const;
  Json Snapshot();
  Json OnCallJson() const;
  Json HudJson() const;

  void BroadcastEvent(const std::string& event, const Json& data);
  void BroadcastState();
  void BroadcastSettings();
  void BroadcastModels();
  void Toast(const std::string& kind, const std::string& message);

  void ApplySettingsPatch(const Json& patch);
  void ApplyLaunchAtLogin();
  void StartModelLoad(bool reload);
  void CreateDictationLocked(DictationMode mode);
  void StartCapture();
  void StartDownload(int key, bool vad);
  void OpenWindow(const std::string& window);

  void OnPartial(const std::string& text, float level);
  void OnFinal(const std::string& text, const std::string& mode,
               bool end_of_hold);
  void OnLevel(float level);
  void OnHotkeyDown();
  void OnHotkeyUp();
  void OnHotkeyCancel();
  void OnHotkeyCaptured(int vk, int mods);

  std::wstring PostProcess(const std::string& text) const;
  std::string AppendHistory(const std::string& text, const std::string& mode);
  void UpdateTrayStatus();

  struct DownloadStatus {
    bool active = false;
    bool ok = false;
    int64_t received = 0;
    int64_t total = 0;
    std::string file;
    std::string error;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::thread thread;
  };

  HINSTANCE instance_ = nullptr;
  DWORD main_thread_id_ = 0;
  bool com_initialized_ = false;
  std::atomic<bool> cleanup_done_{false};
  std::atomic<bool> shutting_down_{false};
  std::atomic<bool> model_loading_{false};
  std::atomic<bool> mic_muted_{false};
  std::atomic<bool> latch_active_{false};

  mutable std::mutex mutex_;  // settings + engine state + on-call transcript
  Settings settings_;
  std::string engine_status_ = "starting";
  std::string engine_error_;
  std::string engine_vad_;
  std::string engine_version_;
  std::string mic_name_;
  std::string last_partial_;
  std::string typed_text_;
  std::string last_final_;
  std::vector<std::string> oncall_segments_;
  bool hud_visible_ = false;
  std::string hud_state_ = "idle";
  std::string hud_text_;
  float hud_level_ = 0.0f;
  std::map<uint64_t, std::string> client_windows_;

  mutable std::mutex dictation_mutex_;
  std::unique_ptr<Dictation> dictation_;
  mutable std::mutex capture_mutex_;

  mutable std::mutex models_mutex_;
  std::map<int, std::unique_ptr<DownloadStatus>> downloads_;

  std::unique_ptr<HistoryStore> history_;
  SherpaEngine asr_;
  WasapiCapture capture_;
  GlobalHotkey hotkey_;
  Tray tray_;
  Launcher launcher_;
  PipeServer pipe_;

  std::thread model_thread_;
};

}  // namespace shuowen
