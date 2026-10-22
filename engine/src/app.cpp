#include "app.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "config.h"
#include "input/cue.h"
#include "input/inserter.h"
#include "input/type_output.h"
#include "util/log.h"
#include "util/text.h"

namespace shuowen {
namespace {

Json Ok(int64_t id, Json data) {
  Json response = Json::Object();
  response.Set("id", Json(id));
  response.Set("ok", Json(true));
  response.Set("data", std::move(data));
  return response;
}

Json Fail(int64_t id, const std::string& code, const std::string& message) {
  Json response = Json::Object();
  response.Set("id", Json(id));
  response.Set("ok", Json(false));
  Json error = Json::Object();
  error.Set("code", Json(code));
  error.Set("message", Json(message));
  response.Set("error", std::move(error));
  return response;
}

std::string ArgString(const Json& args, const std::string& key,
                      const std::string& fallback = "") {
  const Json* value = args.Find(key);
  return value && value->is_string() ? value->AsString() : fallback;
}

int ArgInt(const Json& args, const std::string& key, int fallback) {
  const Json* value = args.Find(key);
  return value && value->is_number() ? value->AsInt() : fallback;
}

bool ArgBool(const Json& args, const std::string& key, bool fallback) {
  const Json* value = args.Find(key);
  return value && value->is_bool() ? value->AsBool() : fallback;
}

std::string LowerAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

bool ContainsAsciiInsensitive(const std::string& haystack,
                              const std::string& needle) {
  if (needle.empty()) return true;
  return LowerAscii(haystack).find(LowerAscii(needle)) != std::string::npos;
}

std::wstring TierDirectoryName(int tier_ms) {
  return L"chunk-" + std::to_wstring(tier_ms) + L"ms-model";
}

bool BundledTierPresent(const ModelPaths& paths) {
  std::error_code ec;
  return std::filesystem::exists(
      paths.bundled_root / TierDirectoryName(paths.tier_ms) /
          (L"encoder-" + std::to_wstring(paths.tier_ms) + L"ms.onnx"),
      ec);
}

Json EntryJson(const HistoryEntry& entry) {
  Json item = Json::Object();
  item.Set("id", Json(entry.id));
  item.Set("timestampMs", Json(entry.timestamp_ms));
  item.Set("text", Json(entry.text));
  item.Set("mode", Json(entry.mode));
  if (!entry.title.empty()) item.Set("title", Json(entry.title));
  if (entry.expires_at_ms > 0) item.Set("expiresAtMs", Json(entry.expires_at_ms));
  Json tags = Json::Array();
  for (const auto& tag : entry.tags) tags.Push(Json(tag));
  item.Set("tags", std::move(tags));
  return item;
}

}  // namespace

App::~App() { Shutdown(); }

bool App::Initialize(HINSTANCE instance) {
  instance_ = instance;
  main_thread_id_ = GetCurrentThreadId();
  const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  com_initialized_ = SUCCEEDED(hr);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    LogF("app: CoInitializeEx failed (0x%08lx)", static_cast<unsigned long>(hr));
    return false;
  }

  settings_ = Settings::Load(paths::DataDir() / L"settings.json");
  mic_muted_.store(settings_.mic_muted);
  history_ = std::make_unique<HistoryStore>(paths::DataDir() / L"history.json");

  if (!pipe_.Start(
          kPipeName,
          [this](uint64_t id, const std::string& line) {
            return HandleLine(id, line);
          },
          [this](uint64_t id) {
            LogF("ipc: client %llu ready", static_cast<unsigned long long>(id));
          },
          [this](uint64_t id) {
            std::lock_guard<std::mutex> lock(mutex_);
            client_windows_.erase(id);
          })) {
    Log("app: failed to start IPC server");
    return false;
  }

  Tray::Callbacks tray_callbacks;
  tray_callbacks.open_popup = [this] { OpenWindow("popup"); };
  tray_callbacks.open_settings = [this] { OpenWindow("settings"); };
  tray_callbacks.open_history = [this] { OpenWindow("history"); };
  tray_callbacks.set_mode = [this](DictationMode mode) {
    Json patch = Json::Object();
    patch.Set("Mode", Json(ModeToJsonProperty(mode)));
    ApplySettingsPatch(patch);
  };
  tray_callbacks.toggle_mute = [this] {
    Settings current = CopySettings();
    Json patch = Json::Object();
    patch.Set("MicMuted", Json(!current.mic_muted));
    ApplySettingsPatch(patch);
  };
  tray_callbacks.toggle_pause = [this] {
    bool paused = false;
    {
      std::lock_guard<std::mutex> lock(dictation_mutex_);
      if (dictation_) {
        dictation_->SetPaused(!dictation_->paused());
        paused = dictation_->paused();
      }
    }
    tray_.SetPaused(paused);
    BroadcastState();
  };
  tray_callbacks.quit = [this] { Quit(); };
  tray_.Create(std::move(tray_callbacks));
  tray_.SetMode(settings_.mode);
  tray_.SetMuted(settings_.mic_muted);

  hotkey_.Install(
      settings_.hotkey_vk, settings_.hotkey_mods,
      [this] { OnHotkeyDown(); }, [this] { OnHotkeyUp(); },
      [this] { OnHotkeyCancel(); },
      [this](int vk, int mods) { OnHotkeyCaptured(vk, mods); });

  ApplyLaunchAtLogin();
  launcher_.EnsureHud();
  StartModelLoad(false);

  if (!settings_.welcomed) OpenWindow("settings");
  if (settings_.oncall_auto_start && settings_.mode == DictationMode::OnCall) {
    OpenWindow("oncall");
  }

  LogF("app: ready v%s, data=%s", kVersion,
       WideToUtf8(paths::DataDir().wstring()).c_str());
  UpdateTrayStatus();
  return true;
}

int App::Run() {
  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  Shutdown();
  return 0;
}

void App::Quit() {
  if (shutting_down_.exchange(true)) return;
  PostThreadMessageW(main_thread_id_, WM_QUIT, 0, 0);
}

void App::Shutdown() {
  if (cleanup_done_.exchange(true)) return;
  Log("app: shutting down");
  BroadcastEvent("shutdown", Json::Object());

  capture_.Stop();
  {
    std::lock_guard<std::mutex> lock(dictation_mutex_);
    dictation_.reset();
  }
  if (model_thread_.joinable()) model_thread_.join();

  {
    std::map<int, std::unique_ptr<DownloadStatus>> tasks;
    {
      std::lock_guard<std::mutex> lock(models_mutex_);
      tasks = std::move(downloads_);
    }
    for (auto& [key, task] : tasks) {
      if (task->cancel) task->cancel->store(true);
    }
    for (auto& [key, task] : tasks) {
      if (task->thread.joinable()) task->thread.join();
    }
  }

  asr_.Unload();
  hotkey_.Uninstall();
  tray_.Destroy();
  pipe_.Stop();
  launcher_.ShutdownAll();

  if (history_) history_->Flush();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.Save(paths::DataDir() / L"settings.json");
  }
  if (com_initialized_) {
    CoUninitialize();
    com_initialized_ = false;
  }
  Log("app: stopped");
}

Settings App::CopySettings() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return settings_;
}

std::string App::SettingsJson() const {
  return CopySettings().ToJson().Dump();
}

std::string App::AppendHistory(const std::string& text,
                               const std::string& mode) {
  if (!history_ || text.empty()) return {};
  bool enabled = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled = settings_.history_enabled;
  }
  const HistoryEntry entry = history_->Append(text, mode, !enabled);
  if (!entry.id.empty()) {
    Json data = Json::Object();
    data.Set("reason", Json("append"));
    BroadcastEvent("history", data);
  }
  return entry.id;
}

Json App::StateJson() const {
  Json state = Json::Object();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state.Set("status", Json(engine_status_));
    state.Set("error", Json(engine_error_));
    state.Set("mode", Json(settings_.ModeCode()));
    state.Set("modeLabel", Json(ModeToHistoryCode(settings_.mode)));
    state.Set("tier", Json(settings_.TierCode()));
    state.Set("vad", Json(engine_vad_));
    state.Set("micMuted", Json(settings_.mic_muted));
    state.Set("micName", Json(mic_name_));
    state.Set("hotkeyVk", Json(settings_.hotkey_vk));
    state.Set("hotkeyMods", Json(settings_.hotkey_mods));
    state.Set("hotkeyName", Json(HotkeyName(settings_.hotkey_vk)));
    state.Set("engineVersion", Json(engine_version_));
    state.Set("oncallCount",
              Json(static_cast<int>(oncall_segments_.size())));
    state.Set("lastFinal", Json(last_final_));
  }
  state.Set("modelLoading", Json(model_loading_.load()));
  bool listening = false;
  bool paused = false;
  {
    std::lock_guard<std::mutex> lock(dictation_mutex_);
    if (dictation_) {
      listening = dictation_->listening();
      paused = dictation_->paused();
    }
  }
  state.Set("listening", Json(listening));
  state.Set("paused", Json(paused));
  return state;
}

Json App::HudJson() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Json hud = Json::Object();
  hud.Set("visible", Json(hud_visible_));
  hud.Set("state", Json(hud_state_));
  hud.Set("text", Json(hud_text_));
  hud.Set("level", Json(hud_level_));
  return hud;
}

Json App::OnCallJson() const {
  Json result = Json::Object();
  Json segments = Json::Array();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& segment : oncall_segments_) {
      segments.Push(Json(segment));
    }
    result.Set("partial", Json(last_partial_));
  }
  bool paused = false;
  {
    std::lock_guard<std::mutex> lock(dictation_mutex_);
    paused = dictation_ && dictation_->paused();
  }
  result.Set("segments", std::move(segments));
  result.Set("paused", Json(paused));
  return result;
}

Json App::ModelsJson() const {
  const Settings settings = CopySettings();
  Json root = Json::Object();
  root.Set("tier", Json(settings.TierMs()));
  root.Set("tierCode", Json(settings.TierCode()));
  root.Set("source", Json(settings.model_source));

  Json tiers = Json::Array();
  for (int ms : {160, 480, 960, 1920}) {
    ModelPaths paths = ModelPaths::ForTier(ms);
    const bool ready = paths.AsrPresent();
    Json item = Json::Object();
    item.Set("tier", Json(ms));
    item.Set("tierCode", Json("Ms" + std::to_string(ms)));
    item.Set("ready", Json(ready));
    item.Set("bundled", Json(BundledTierPresent(paths)));
    item.Set("active", Json(ms == settings.TierMs()));

    bool downloading = false;
    bool failed = false;
    bool ok = false;
    int64_t received = 0;
    int64_t total = 0;
    std::string error;
    std::string file;
    {
      std::lock_guard<std::mutex> lock(models_mutex_);
      auto it = downloads_.find(ms);
      if (it != downloads_.end()) {
        downloading = it->second->active;
        failed = !it->second->error.empty();
        ok = it->second->ok;
        received = it->second->received;
        total = it->second->total;
        error = it->second->error;
        file = it->second->file;
      }
    }
    if (downloading) {
      item.Set("state", Json("downloading"));
    } else if (failed) {
      item.Set("state", Json("error"));
    } else if (ready || ok) {
      item.Set("state", Json("ready"));
    } else {
      item.Set("state", Json("missing"));
    }
    item.Set("received", Json(received));
    item.Set("total", Json(total));
    if (!error.empty()) item.Set("error", Json(error));
    if (!file.empty()) item.Set("file", Json(file));
    tiers.Push(std::move(item));
  }
  root.Set("tiers", std::move(tiers));

  ModelPaths paths = ModelPaths::ForTier(settings.TierMs());
  Json vad = Json::Object();
  vad.Set("mode", Json(settings.VadCode()));
  vad.Set("ready", Json(paths.SileroPresent()));
  vad.Set("active", Json(paths.SileroPresent() ? "silero" : "energy"));
  bool vad_downloading = false;
  bool vad_failed = false;
  int64_t vad_received = 0;
  int64_t vad_total = 0;
  {
    std::lock_guard<std::mutex> lock(models_mutex_);
    auto it = downloads_.find(-1);
    if (it != downloads_.end()) {
      vad_downloading = it->second->active;
      vad_failed = !it->second->error.empty();
      vad_received = it->second->received;
      vad_total = it->second->total;
    }
  }
  vad.Set("downloading", Json(vad_downloading));
  vad.Set("failed", Json(vad_failed));
  vad.Set("received", Json(vad_received));
  vad.Set("total", Json(vad_total));
  root.Set("vad", std::move(vad));
  return root;
}

Json App::Snapshot() {
  Json root = Json::Object();
  root.Set("version", Json(kVersion));
  root.Set("engine", StateJson());
  root.Set("settings", CopySettings().ToJson());
  root.Set("models", ModelsJson());
  root.Set("hud", HudJson());
  root.Set("oncall", OnCallJson());

  Json history = Json::Object();
  Json recent = Json::Array();
  if (history_) {
    history.Set("lifetimeChars", Json(history_->LifetimeChars()));
    history.Set("count", Json(history_->Count()));
    const auto entries = history_->List();
    size_t count = 0;
    for (const auto& entry : entries) {
      if (count++ >= 5) break;
      recent.Push(EntryJson(entry));
    }
  } else {
    history.Set("lifetimeChars", Json(0));
    history.Set("count", Json(0));
  }
  history.Set("recent", std::move(recent));
  root.Set("history", std::move(history));
  return root;
}

void App::BroadcastEvent(const std::string& event, const Json& data) {
  Json envelope = Json::Object();
  envelope.Set("event", Json(event));
  envelope.Set("data", data);
  pipe_.Broadcast(envelope.Dump());
}

void App::BroadcastState() {
  BroadcastEvent("state", StateJson());
  UpdateTrayStatus();
}

void App::BroadcastSettings() {
  BroadcastEvent("settings", CopySettings().ToJson());
}

void App::BroadcastModels() { BroadcastEvent("models", ModelsJson()); }

void App::Toast(const std::string& kind, const std::string& message) {
  Json data = Json::Object();
  data.Set("kind", Json(kind));
  data.Set("message", Json(message));
  BroadcastEvent("toast", data);
}

void App::UpdateTrayStatus() {
  std::string status;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    status = engine_status_;
  }
  bool listening = false;
  {
    std::lock_guard<std::mutex> lock(dictation_mutex_);
    listening = dictation_ && dictation_->listening();
  }
  std::wstring tooltip = L"说文 ShuoWen · ";
  if (model_loading_.load() || status == "loading") {
    tooltip += L"模型加载中…";
  } else if (status == "error") {
    tooltip += L"引擎错误";
  } else if (status == "ready") {
    tooltip += listening ? L"聆听中…" : L"就绪";
  } else {
    tooltip += L"启动中…";
  }
  tray_.SetTooltip(tooltip);
}

void App::StartModelLoad(bool reload) {
  if (model_loading_.exchange(true)) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    engine_status_ = "loading";
    engine_error_.clear();
    hud_visible_ = false;
    hud_state_ = "idle";
    hud_text_.clear();
  }
  BroadcastState();

  if (reload) {
    {
      std::lock_guard<std::mutex> capture_lock(capture_mutex_);
      capture_.Stop();
    }
    {
      std::lock_guard<std::mutex> lock(dictation_mutex_);
      dictation_.reset();
    }
    asr_.Unload();
  }
  if (model_thread_.joinable()) model_thread_.join();

  const Settings snapshot = CopySettings();
  model_thread_ = std::thread([this, snapshot] {
    ModelPaths paths = ModelPaths::ForTier(snapshot.TierMs());
    std::string error;
    LogF("engine: loading tier=%dms", paths.tier_ms);
    const bool ok = asr_.Load(paths, snapshot.vad, &error);
    if (shutting_down_.load()) {
      model_loading_.store(false);
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      engine_status_ = ok ? "ready" : "error";
      engine_error_ = error;
      engine_vad_ = ok ? asr_.VadName() : "";
      engine_version_ = ok ? asr_.Version() : "";
    }

    if (ok) {
      {
        std::lock_guard<std::mutex> lock(dictation_mutex_);
        CreateDictationLocked(snapshot.mode);
      }
      if (!shutting_down_.load()) StartCapture();
    } else {
      Toast("error", error);
    }

    BroadcastState();
    BroadcastModels();
    if (ok && snapshot.oncall_auto_start &&
        snapshot.mode == DictationMode::OnCall && !shutting_down_.load()) {
      OpenWindow("oncall");
    }
    model_loading_.store(false);
    BroadcastState();
    UpdateTrayStatus();
  });
}

void App::CreateDictationLocked(DictationMode mode) {
  dictation_ = std::make_unique<Dictation>(asr_, mode);
  dictation_->OnPartial(
      [this](const std::string& text, float level) { OnPartial(text, level); });
  dictation_->OnFinal(
      [this](const std::string& text, const std::string& mode_code,
             bool end_of_hold) { OnFinal(text, mode_code, end_of_hold); });
  dictation_->OnLevel([this](float level) { OnLevel(level); });
  dictation_->Start();
  Log("engine: dictation worker started");
}

void App::StartCapture() {
  std::lock_guard<std::mutex> capture_lock(capture_mutex_);
  capture_.Stop();
  const Settings settings = CopySettings();
  mic_muted_.store(settings.mic_muted);
  const bool ok = capture_.Start(
      settings.mic_device_id,
      [this](const float* samples, size_t count) {
        if (mic_muted_.load(std::memory_order_relaxed)) return;
        std::lock_guard<std::mutex> lock(dictation_mutex_);
        if (dictation_) dictation_->PushFrame(samples, count);
      },
      [this](const std::string& error) { Toast("error", error); });
  {
    std::lock_guard<std::mutex> lock(mutex_);
    mic_name_ = ok ? capture_.device_name() : "";
  }
  if (!ok) Toast("error", "无法启动麦克风，请检查系统隐私设置");
  BroadcastState();
}

void App::ApplySettingsPatch(const Json& patch) {
  Settings before;
  Settings after;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    before = settings_;
    settings_.ApplyJson(patch);
    settings_.Save(paths::DataDir() / L"settings.json");
    after = settings_;
  }

  mic_muted_.store(after.mic_muted);
  tray_.SetMode(after.mode);
  tray_.SetMuted(after.mic_muted);

  if (before.hotkey_vk != after.hotkey_vk ||
      before.hotkey_mods != after.hotkey_mods) {
    hotkey_.SetBinding(after.hotkey_vk, after.hotkey_mods);
  }

  if (before.mode != after.mode) {
    {
      std::lock_guard<std::mutex> lock(dictation_mutex_);
      if (dictation_) dictation_->SetMode(after.mode);
    }
    if (after.mode == DictationMode::OnCall) OpenWindow("oncall");
  }

  if (before.launch_at_login != after.launch_at_login) ApplyLaunchAtLogin();
  if (before.mic_device_id != after.mic_device_id) StartCapture();
  if (!before.mic_muted && after.mic_muted) {
    Json level = Json::Object();
    level.Set("value", Json(0.0));
    BroadcastEvent("level", level);
  }

  if (before.tier != after.tier || before.vad != after.vad) {
    StartModelLoad(true);
  }

  if (!before.oncall_auto_start && after.oncall_auto_start &&
      after.mode == DictationMode::OnCall) {
    OpenWindow("oncall");
  }

  BroadcastSettings();
  BroadcastState();
  BroadcastModels();
}

void App::ApplyLaunchAtLogin() {
  const Settings settings = CopySettings();
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                    KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
    return;
  }
  if (settings.launch_at_login) {
    const std::wstring command =
        L"\"" + paths::ExePath().wstring() + L"\" --autostart";
    RegSetValueExW(key, L"ShuoWen", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(command.c_str()),
                   static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, L"ShuoWen");
  }
  RegCloseKey(key);
}

void App::StartDownload(int key, bool vad) {
  const Settings settings = CopySettings();

  std::unique_ptr<DownloadStatus> old;
  {
    std::lock_guard<std::mutex> lock(models_mutex_);
    auto it = downloads_.find(key);
    if (it != downloads_.end()) {
      if (it->second->active) return;
      old = std::move(it->second);
      downloads_.erase(it);
    }
  }
  if (old && old->thread.joinable()) old->thread.join();

  auto status = std::make_unique<DownloadStatus>();
  status->active = true;
  status->cancel = std::make_shared<std::atomic<bool>>(false);
  DownloadStatus* raw = status.get();

  {
    std::lock_guard<std::mutex> lock(models_mutex_);
    downloads_[key] = std::move(status);
    raw->thread = std::thread([this, key, vad, settings, raw] {
      auto last_broadcast = std::chrono::steady_clock::now();
      auto progress = [&](const DownloadProgress& item) {
        {
          std::lock_guard<std::mutex> lock(models_mutex_);
          raw->received = item.received;
          raw->total = item.total;
          raw->file = item.file;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - last_broadcast >= std::chrono::milliseconds(200)) {
          last_broadcast = now;
          BroadcastModels();
        }
      };

      std::string error;
      bool ok = false;
      if (vad) {
        ok = EnsureVad(ModelPaths::ForTier(settings.TierMs()), progress,
                       *raw->cancel, &error);
      } else {
        ok = EnsureTier(ModelPaths::ForTier(key), settings.model_source,
                        progress, *raw->cancel, &error);
      }

      {
        std::lock_guard<std::mutex> lock(models_mutex_);
        raw->active = false;
        raw->ok = ok;
        raw->error = ok ? "" : (error.empty() ? "download failed" : error);
      }
      BroadcastModels();
      if (ok) {
        Toast("success", vad ? "VAD 模型已下载" : "模型已下载");
        if (!vad && key == CopySettings().TierMs()) StartModelLoad(true);
      } else {
        Toast("error", vad ? "VAD 下载失败" : "模型下载失败");
      }
    });
  }
}

void App::OpenWindow(const std::string& window) {
  if (window == "hud") {
    launcher_.EnsureHud();
    return;
  }
  if (window == "oncall") {
    launcher_.EnsureOnCall();
    BroadcastEvent("oncall", OnCallJson());
    return;
  }
  if (launcher_.IsRunning(window)) {
    Json data = Json::Object();
    data.Set("window", Json(window));
    BroadcastEvent("focus", data);
    return;
  }
  launcher_.Open(window);
}

void App::OnPartial(const std::string& text, float level) {
  const Settings settings = CopySettings();
  std::string typed;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    typed = typed_text_;
  }
  const TypeOutputPlan plan =
      PlanTypeOutput(typed, text, settings.output_traditional);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_partial_ = plan.next_utf8;
    hud_visible_ = true;
    hud_state_ = "listening";
    hud_text_ = plan.next_utf8;
    hud_level_ = level;
  }

  if (settings.mode == DictationMode::Type && !plan.next_utf8.empty() &&
      plan.changed) {
    TextInserter::ReplaceWith(plan.previous, plan.next);
    std::lock_guard<std::mutex> lock(mutex_);
    typed_text_ = plan.next_utf8;
  }

  Json data = Json::Object();
  data.Set("text", Json(plan.next_utf8));
  data.Set("level", Json(level));
  BroadcastEvent("partial", data);
  BroadcastEvent("hud", HudJson());
  if (settings.mode == DictationMode::OnCall) {
    BroadcastEvent("oncall", OnCallJson());
  }
}

void App::OnFinal(const std::string& text, const std::string& mode,
                  bool end_of_hold) {
  const Settings settings = CopySettings();
  const std::wstring processed =
      PostProcessText(text, settings.output_traditional);
  const std::string final_text = WideToUtf8(processed);
  std::string typed;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    typed = typed_text_;
  }

  if (mode == "oncall") {
    std::string history_id;
    if (!final_text.empty()) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        oncall_segments_.push_back(final_text);
        if (oncall_segments_.size() > 200) oncall_segments_.erase(oncall_segments_.begin());
        last_partial_.clear();
        last_final_ = final_text;
      }
      history_id = AppendHistory(final_text, "oncall");
      if (settings.cue_enabled) {
        Cue::Play(false, settings.cue_theme, settings.cue_volume);
      }
    } else {
      std::lock_guard<std::mutex> lock(mutex_);
      last_partial_.clear();
    }
    Json data = Json::Object();
    data.Set("text", Json(final_text));
    data.Set("mode", Json("oncall"));
    data.Set("endOfHold", Json(end_of_hold));
    data.Set("historyId", Json(history_id));
    BroadcastEvent("final", data);
    BroadcastEvent("oncall", OnCallJson());
    BroadcastState();
    return;
  }

  if (!final_text.empty()) {
    if (mode == "type") {
      TextInserter::ReplaceWith(Utf8ToWide(typed), processed);
    } else {
      TextInserter::InsertText(processed);
    }
    if (settings.clipboard_overwrite) {
      TextInserter::CopyToClipboard(processed);
    }
    const std::string history_id = AppendHistory(final_text, mode);
    if (settings.cue_enabled) {
      Cue::Play(false, settings.cue_theme, settings.cue_volume);
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      typed_text_.clear();
      last_partial_.clear();
      last_final_ = final_text;
      hud_visible_ = true;
      hud_state_ = "done";
      hud_text_ = final_text;
      hud_level_ = 0.0f;
    }
    Json data = Json::Object();
    data.Set("text", Json(final_text));
    data.Set("mode", Json(mode));
    data.Set("endOfHold", Json(end_of_hold));
    data.Set("historyId", Json(history_id));
    BroadcastEvent("final", data);
    BroadcastEvent("hud", HudJson());
  } else {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      typed_text_.clear();
      last_partial_.clear();
      hud_visible_ = false;
      hud_state_ = "idle";
      hud_text_.clear();
      hud_level_ = 0.0f;
    }
    Json data = Json::Object();
    data.Set("text", Json(""));
    data.Set("mode", Json(mode));
    data.Set("endOfHold", Json(end_of_hold));
    BroadcastEvent("final", data);
    BroadcastEvent("hud", HudJson());
  }
  BroadcastState();
}

void App::OnLevel(float level) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    hud_level_ = level;
  }
  Json data = Json::Object();
  data.Set("value", Json(level));
  BroadcastEvent("level", data);
}

void App::OnHotkeyDown() {
  if (shutting_down_.load()) return;
  if (model_loading_.load()) {
    Toast("info", "模型加载中，请稍候…");
    return;
  }
  const Settings settings = CopySettings();
  if (settings.mode == DictationMode::OnCall) return;

  bool begin = settings.trigger == TriggerMode::Hold;
  if (settings.trigger != TriggerMode::Hold) {
    begin = !latch_active_.load();
    latch_active_.store(begin);
  }
  if (begin && settings.cue_enabled) {
    Cue::Play(true, settings.cue_theme, settings.cue_volume);
  }
  if (begin) launcher_.EnsureHud();

  {
    std::lock_guard<std::mutex> lock(dictation_mutex_);
    if (!dictation_) {
      Toast("error", "引擎尚未就绪");
      return;
    }
    if (begin) dictation_->BeginHold();
    else dictation_->EndHold();
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (begin) {
      typed_text_.clear();
      last_partial_.clear();
      hud_visible_ = true;
      hud_state_ = "listening";
      hud_text_.clear();
      hud_level_ = 0.0f;
    } else {
      hud_state_ = "processing";
    }
  }
  BroadcastEvent("hud", HudJson());
  BroadcastState();
}

void App::OnHotkeyUp() {
  const Settings settings = CopySettings();
  if (settings.mode == DictationMode::OnCall) return;
  if (settings.trigger != TriggerMode::Hold) return;
  bool was_capturing = false;
  {
    std::lock_guard<std::mutex> lock(dictation_mutex_);
    if (dictation_) {
      was_capturing = dictation_->capturing();
      if (was_capturing) dictation_->EndHold();
    }
  }
  if (!was_capturing) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    hud_state_ = "processing";
  }
  BroadcastEvent("hud", HudJson());
  BroadcastState();
}

void App::OnHotkeyCancel() {
  {
    std::lock_guard<std::mutex> lock(dictation_mutex_);
    if (dictation_) dictation_->Cancel();
  }
  latch_active_.store(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    typed_text_.clear();
    last_partial_.clear();
    hud_visible_ = false;
    hud_state_ = "idle";
    hud_text_.clear();
  }
  BroadcastEvent("hud", HudJson());
  BroadcastState();
}

void App::OnHotkeyCaptured(int vk, int mods) {
  Settings after;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.hotkey_vk = vk;
    settings_.hotkey_mods = mods;
    settings_.Save(paths::DataDir() / L"settings.json");
    after = settings_;
  }
  hotkey_.SetBinding(vk, mods);
  Json data = Json::Object();
  data.Set("vk", Json(vk));
  data.Set("mods", Json(mods));
  data.Set("name", Json(HotkeyName(vk)));
  BroadcastEvent("hotkey.captured", data);
  BroadcastSettings();
  BroadcastState();
}

std::string App::HandleLine(uint64_t client_id, const std::string& line) {
  std::string parse_error;
  Json root = Json::Parse(line, &parse_error);
  if (!root.is_object()) {
    return Fail(0, "bad_json", parse_error.empty() ? "invalid JSON" : parse_error)
        .Dump();
  }
  const Json* id_node = root.Find("id");
  const int64_t id = id_node && id_node->is_number() ? id_node->AsInt64() : 0;
  const std::string cmd =
      root.Find("cmd") && root.Find("cmd")->is_string()
          ? root.Find("cmd")->AsString()
          : "";
  const Json args =
      root.Find("args") && root.Find("args")->is_object()
          ? *root.Find("args")
          : Json::Object();
  if (cmd == "hello" || cmd == "ui.ready") {
    const std::string window = ArgString(args, "window");
    if (!window.empty()) {
      std::lock_guard<std::mutex> lock(mutex_);
      client_windows_[client_id] = window;
    }
    if (cmd == "hello") return Ok(id, Snapshot()).Dump();
    Json data = Json::Object();
    data.Set("ok", Json(true));
    return Ok(id, std::move(data)).Dump();
  }

  if (cmd == "settings.get") {
    return Ok(id, CopySettings().ToJson()).Dump();
  }

  if (cmd == "settings.patch") {
    const Json* patch = args.Find("patch");
    if (!patch || !patch->is_object()) {
      return Fail(id, "bad_args", "patch must be an object").Dump();
    }
    ApplySettingsPatch(*patch);
    return Ok(id, CopySettings().ToJson()).Dump();
  }

  if (cmd == "history.list") {
    const std::string query = ArgString(args, "query");
    const int limit = std::max(1, ArgInt(args, "limit", 500));
    Json data = Json::Object();
    Json entries = Json::Array();
    int matched = 0;
    if (history_) {
      for (const auto& entry : history_->List()) {
        if (!ContainsAsciiInsensitive(entry.text, query)) continue;
        ++matched;
        if (matched <= limit) entries.Push(EntryJson(entry));
      }
      data.Set("lifetimeChars", Json(history_->LifetimeChars()));
      data.Set("count", Json(history_->Count()));
    } else {
      data.Set("lifetimeChars", Json(0));
      data.Set("count", Json(0));
    }
    data.Set("entries", std::move(entries));
    return Ok(id, std::move(data)).Dump();
  }

  if (cmd == "history.update") {
    if (!history_) return Fail(id, "unavailable", "history not ready").Dump();
    std::vector<std::string> tags;
    const Json* tags_node = args.Find("tags");
    if (tags_node && tags_node->is_array()) {
      for (const auto& tag : tags_node->AsArray()) {
        if (tag.is_string()) tags.push_back(tag.AsString());
      }
    }
    const bool ok = history_->Update(ArgString(args, "id"),
                                     ArgString(args, "text"),
                                     ArgString(args, "title"), tags);
    Json data = Json::Object();
    data.Set("updated", Json(ok));
    if (ok) BroadcastEvent("history", data);
    return Ok(id, std::move(data)).Dump();
  }

  if (cmd == "history.delete") {
    const bool ok = history_ && history_->Remove(ArgString(args, "id"));
    Json data = Json::Object();
    data.Set("deleted", Json(ok));
    if (ok) BroadcastEvent("history", data);
    return Ok(id, std::move(data)).Dump();
  }

  if (cmd == "history.clear") {
    if (history_) history_->Clear();
    Json data = Json::Object();
    data.Set("cleared", Json(true));
    BroadcastEvent("history", data);
    return Ok(id, std::move(data)).Dump();
  }

  if (cmd == "history.export") {
    if (!history_) return Fail(id, "unavailable", "history not ready").Dump();
    std::filesystem::path target = Utf8ToWide(ArgString(args, "path"));
    if (target.empty()) {
      PWSTR documents = nullptr;
      std::filesystem::path dir;
      if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr,
                                         &documents))) {
        dir = documents;
        CoTaskMemFree(documents);
      } else {
        dir = paths::DataDir();
      }
      SYSTEMTIME now;
      GetLocalTime(&now);
      wchar_t name[64];
      swprintf(name, 64, L"ShuoWen-history-%04d%02d%02d-%02d%02d%02d.txt",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
               now.wSecond);
      target = dir / name;
    }
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (out) {
      for (const auto& entry : history_->List()) {
        out << entry.timestamp_ms << "\t" << entry.mode << "\t" << entry.text
            << "\n";
      }
    }
    Json data = Json::Object();
    data.Set("path", Json(WideToUtf8(target.wstring())));
    data.Set("ok", Json(static_cast<bool>(out)));
    return Ok(id, std::move(data)).Dump();
  }

  if (cmd == "models.status") {
    return Ok(id, ModelsJson()).Dump();
  }

  if (cmd == "models.download") {
    const int tier = ArgInt(args, "tier", CopySettings().TierMs());
    StartDownload(tier, false);
    return Ok(id, ModelsJson()).Dump();
  }

  if (cmd == "models.download_vad") {
    StartDownload(-1, true);
    return Ok(id, ModelsJson()).Dump();
  }

  if (cmd == "models.cancel") {
    const int key = ArgBool(args, "vad", false) ? -1 : ArgInt(args, "tier", 0);
    {
      std::lock_guard<std::mutex> lock(models_mutex_);
      auto it = downloads_.find(key);
      if (it != downloads_.end() && it->second->cancel) {
        it->second->cancel->store(true);
      }
    }
    return Ok(id, ModelsJson()).Dump();
  }

  if (cmd == "models.delete") {
    const int tier = ArgInt(args, "tier", 0);
    if (tier == 160 || tier == 480 || tier == 960 || tier == 1920) {
      std::error_code ec;
      std::filesystem::remove_all(ModelPaths::ForTier(tier).TierDir(), ec);
    }
    BroadcastModels();
    return Ok(id, ModelsJson()).Dump();
  }

  if (cmd == "models.use") {
    Json patch = Json::Object();
    patch.Set("Tier", Json("Ms" + std::to_string(ArgInt(args, "tier", 960))));
    ApplySettingsPatch(patch);
    return Ok(id, ModelsJson()).Dump();
  }

  if (cmd == "engine.status") {
    return Ok(id, StateJson()).Dump();
  }

  if (cmd == "engine.reload") {
    StartModelLoad(true);
    return Ok(id, StateJson()).Dump();
  }

  if (cmd == "dictation.start") {
    if (CopySettings().mode == DictationMode::OnCall) OpenWindow("oncall");
    else OnHotkeyDown();
    return Ok(id, StateJson()).Dump();
  }

  if (cmd == "dictation.stop") {
    OnHotkeyUp();
    return Ok(id, StateJson()).Dump();
  }

  if (cmd == "dictation.toggle") {
    OnHotkeyDown();
    return Ok(id, StateJson()).Dump();
  }

  if (cmd == "dictation.pause") {
    const bool paused = ArgBool(args, "paused", true);
    {
      std::lock_guard<std::mutex> lock(dictation_mutex_);
      if (dictation_) dictation_->SetPaused(paused);
    }
    tray_.SetPaused(paused);
    BroadcastState();
    BroadcastEvent("oncall", OnCallJson());
    return Ok(id, StateJson()).Dump();
  }

  if (cmd == "dictation.copy") {
    std::string text = ArgString(args, "text");
    if (text.empty()) {
      std::lock_guard<std::mutex> lock(mutex_);
      text = last_final_;
      if (text.empty()) {
        for (size_t i = 0; i < oncall_segments_.size(); ++i) {
          if (i) text += "\n";
          text += oncall_segments_[i];
        }
      }
    }
    TextInserter::CopyToClipboard(Utf8ToWide(text));
    Json data = Json::Object();
    data.Set("copied", Json(true));
    return Ok(id, std::move(data)).Dump();
  }

  if (cmd == "dictation.clear") {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      oncall_segments_.clear();
      last_final_.clear();
      last_partial_.clear();
    }
    BroadcastEvent("oncall", OnCallJson());
    return Ok(id, OnCallJson()).Dump();
  }

  if (cmd == "mic.devices") {
    Json devices = Json::Array();
    for (const auto& [device_id, name] : WasapiCapture::Devices()) {
      Json item = Json::Object();
      item.Set("id", Json(device_id));
      item.Set("name", Json(name));
      devices.Push(std::move(item));
    }
    Json data = Json::Object();
    data.Set("devices", std::move(devices));
    return Ok(id, std::move(data)).Dump();
  }

  if (cmd == "mic.set") {
    Json patch = Json::Object();
    patch.Set("MicDeviceId", Json(ArgString(args, "id")));
    ApplySettingsPatch(patch);
    return Ok(id, StateJson()).Dump();
  }

  if (cmd == "mic.mute") {
    Json patch = Json::Object();
    patch.Set("MicMuted", Json(ArgBool(args, "muted", true)));
    ApplySettingsPatch(patch);
    return Ok(id, StateJson()).Dump();
  }

  if (cmd == "hotkey.capture") {
    hotkey_.SetCaptureMode(true);
    Json data = Json::Object();
    data.Set("capturing", Json(true));
    return Ok(id, std::move(data)).Dump();
  }

  if (cmd == "app.open") {
    OpenWindow(ArgString(args, "window", "settings"));
    return Ok(id, StateJson()).Dump();
  }

  if (cmd == "app.openDataDir") {
    ShellExecuteW(nullptr, L"open", paths::DataDir().c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
    Json data = Json::Object();
    data.Set("path", Json(WideToUtf8(paths::DataDir().wstring())));
    return Ok(id, std::move(data)).Dump();
  }

  if (cmd == "app.quit") {
    Quit();
    Json data = Json::Object();
    data.Set("quitting", Json(true));
    return Ok(id, std::move(data)).Dump();
  }

  return Fail(id, "unknown_command", "unknown command: " + cmd).Dump();
}

}  // namespace shuowen
