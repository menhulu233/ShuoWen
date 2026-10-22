#include "shuowen_ipc.h"

#include <algorithm>
#include <chrono>
#include <vector>

namespace {

std::wstring Utf8ToWide(const std::string& text) {
  if (text.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), length);
  return out;
}

void LogNative(const std::string& message) {
  wchar_t temp[MAX_PATH];
  const DWORD length = GetTempPathW(MAX_PATH, temp);
  if (length == 0) return;
  const std::wstring path = std::wstring(temp) + L"shuowen_ipc_" +
                            std::to_wstring(GetCurrentProcessId()) + L".log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  const std::string line = message + "\r\n";
  DWORD written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written,
            nullptr);
  CloseHandle(file);
}

RECT ActiveMonitorWorkArea() {
  RECT fallback{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &fallback, 0);
  const HWND foreground = GetForegroundWindow();
  if (!foreground) return fallback;
  const HMONITOR monitor =
      MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
  if (!monitor) return fallback;
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) return fallback;
  return info.rcWork;
}

}  // namespace

ShuowenIpc::ShuowenIpc(HWND window, flutter::FlutterEngine* engine)
    : window_(window), engine_(engine), shared_(std::make_shared<SharedState>()) {
  auto* messenger = engine_->messenger();
  method_channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          messenger, "shuowen/ipc", &flutter::StandardMethodCodec::GetInstance());
  method_channel_->SetMethodCallHandler(
      [this](const flutter::MethodCall<flutter::EncodableValue>& call,
             std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
                 result) {
        if (call.method_name() == "connect") {
          const auto* name = std::get_if<std::string>(call.arguments());
          if (!name) {
            result->Error("bad_args", "pipe name must be a string");
            return;
          }
          result->Success(flutter::EncodableValue(Connect(*name)));
        } else if (call.method_name() == "send") {
          const auto* line = std::get_if<std::string>(call.arguments());
          if (!line) {
            result->Error("bad_args", "line must be a string");
            return;
          }
          result->Success(flutter::EncodableValue(Send(*line)));
        } else if (call.method_name() == "disconnect") {
          Disconnect();
          result->Success();
        } else if (call.method_name() == "window.showInactive") {
          const auto* no_activate = std::get_if<bool>(call.arguments());
          ShowInactive(no_activate != nullptr && *no_activate);
          result->Success();
        } else if (call.method_name() == "window.showHud") {
          ShowHud();
          result->Success();
        } else if (call.method_name() == "window.hide") {
          HideWindow();
          result->Success();
        } else {
          result->NotImplemented();
        }
      });

  event_channel_ =
      std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
          messenger, "shuowen/ipc/events",
          &flutter::StandardMethodCodec::GetInstance());
  auto state = shared_;
  auto handler = std::make_unique<
      flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
      [state](const flutter::EncodableValue* arguments,
              std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&&
                  events)
          -> std::unique_ptr<
              flutter::StreamHandlerError<flutter::EncodableValue>> {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->alive) return nullptr;
        state->sink = std::move(events);
        for (auto& line : state->pending) {
          state->sink->Success(flutter::EncodableValue(line));
        }
        state->pending.clear();
        return nullptr;
      },
      [state](const flutter::EncodableValue* arguments)
          -> std::unique_ptr<
              flutter::StreamHandlerError<flutter::EncodableValue>> {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->sink.reset();
        return nullptr;
      });
  event_channel_->SetStreamHandler(std::move(handler));
}

ShuowenIpc::~ShuowenIpc() {
  Disconnect();
  {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->alive = false;
    shared_->sink.reset();
    shared_->pending.clear();
  }
  if (method_channel_) method_channel_->SetMethodCallHandler(nullptr);
  if (event_channel_) event_channel_->SetStreamHandler(nullptr);
}

void ShuowenIpc::ShowInactive(bool no_activate) {
  if (!window_) return;
  if (no_activate) {
    // The HUD is a passive overlay: it must never take keyboard focus away
    // from the app the user is dictating into. window_manager's show() always
    // calls SetForegroundWindow, so panels use this native path instead.
    const LONG_PTR style = GetWindowLongPtrW(window_, GWL_EXSTYLE);
    SetWindowLongPtrW(window_, GWL_EXSTYLE, style | WS_EX_NOACTIVATE);
  }
  ShowWindow(window_, SW_SHOWNOACTIVATE);
  SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void ShuowenIpc::ShowHud() {
  if (!window_) return;
  // The HUD follows the monitor that owns the foreground app. This matters
  // when the user dictates on a secondary display; the process may have been
  // launched while another monitor was active.
  const LONG_PTR style = GetWindowLongPtrW(window_, GWL_EXSTYLE);
  SetWindowLongPtrW(window_, GWL_EXSTYLE, style | WS_EX_NOACTIVATE);
  RECT bounds{};
  if (GetWindowRect(window_, &bounds)) {
    const RECT work = ActiveMonitorWorkArea();
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const int x = work.left + (work.right - work.left - width) / 2;
    const int y = work.bottom - height - 44;
    SetWindowPos(window_, HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  } else {
    ShowWindow(window_, SW_SHOWNOACTIVATE);
  }
}

void ShuowenIpc::HideWindow() {
  if (window_) ShowWindow(window_, SW_HIDE);
}

bool ShuowenIpc::Connect(const std::string& pipe_name) {
  Disconnect();
  const std::wstring name = Utf8ToWide(pipe_name);
  if (name.empty()) return false;

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  HANDLE pipe = INVALID_HANDLE_VALUE;
  while (std::chrono::steady_clock::now() < deadline) {
    pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                       OPEN_EXISTING, 0, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) break;
    WaitNamedPipeW(name.c_str(), 200);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (pipe == INVALID_HANDLE_VALUE) {
    LogNative("connect failed: " + std::to_string(GetLastError()));
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(pipe_mutex_);
    pipe_ = pipe;
    running_.store(true);
  }
  LogNative("pipe connected");
  reader_ = std::thread([this] { ReadLoop(); });
  return true;
}

bool ShuowenIpc::Send(const std::string& line) {
  std::lock_guard<std::mutex> send_lock(send_mutex_);
  HANDLE pipe = INVALID_HANDLE_VALUE;
  {
    std::lock_guard<std::mutex> lock(pipe_mutex_);
    pipe = pipe_;
  }
  if (pipe == INVALID_HANDLE_VALUE) return false;

  std::string payload = line;
  payload.push_back('\n');
  size_t offset = 0;
  while (offset < payload.size()) {
    DWORD written = 0;
    const DWORD chunk = static_cast<DWORD>(
        std::min<size_t>(payload.size() - offset, 32 * 1024));
    if (!WriteFile(pipe, payload.data() + offset, chunk, &written, nullptr) ||
        written == 0) {
      LogNative("send failed: " + std::to_string(GetLastError()));
      return false;
    }
    offset += written;
  }
  return true;
}

void ShuowenIpc::Disconnect() {
  running_.store(false);
  if (reader_.joinable()) reader_.join();
  std::lock_guard<std::mutex> lock(pipe_mutex_);
  if (pipe_ != INVALID_HANDLE_VALUE) {
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
  }
  LogNative("pipe disconnected");
}
void ShuowenIpc::Emit(const std::string& line) {
  if (!engine_) return;
  if (engine_->IsPlatformThread()) {
    EmitOnPlatform(line);
    return;
  }
  auto* copy = new std::string(line);
  if (!window_ || !PostMessageW(window_, kPipeMessage, 0,
                                reinterpret_cast<LPARAM>(copy))) {
    delete copy;
    return;
  }
}

void ShuowenIpc::HandlePostedLine(const std::string& line) {
  EmitOnPlatform(line);
}

void ShuowenIpc::EmitOnPlatform(const std::string& line) {
  auto state = shared_;
  std::lock_guard<std::mutex> lock(state->mutex);
  if (!state->alive) return;
  if (state->sink) {
    state->sink->Success(flutter::EncodableValue(line));
    return;
  }
  if (state->pending.size() >= 256) state->pending.pop_front();
  state->pending.push_back(line);
}

void ShuowenIpc::ReadLoop() {
  constexpr DWORD kBufferSize = 8192;
  char buffer[kBufferSize];
  std::string pending;

  while (running_.load()) {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    {
      std::lock_guard<std::mutex> lock(pipe_mutex_);
      pipe = pipe_;
    }
    if (pipe == INVALID_HANDLE_VALUE) break;

    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
      LogNative("peek failed: " + std::to_string(GetLastError()));
      break;
    }
    if (available == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
      continue;
    }

    DWORD read = 0;
    const DWORD chunk = std::min<DWORD>(available, kBufferSize);
    if (!ReadFile(pipe, buffer, chunk, &read, nullptr) || read == 0) {
      LogNative("read failed: " + std::to_string(GetLastError()));
      break;
    }
    pending.append(buffer, read);
    size_t newline = 0;
    while ((newline = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, newline);
      pending.erase(0, newline + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!line.empty()) {
        Emit(line);
      }
    }
    if (pending.size() > 4 * 1024 * 1024) pending.clear();
  }

  Emit("__shuowen_closed__");
  LogNative("read loop exit");
}
