#ifndef RUNNER_SHUOWEN_IPC_H_
#define RUNNER_SHUOWEN_IPC_H_

#include <flutter/encodable_value.h>
#include <flutter/event_channel.h>
#include <flutter/event_sink.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/flutter_engine.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include <windows.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Native named-pipe client for the Flutter renderer.
//
// A background thread owns the pipe I/O. Lines are handed to the window
// thread with PostMessage and only there reach the EventSink: EventChannel
// sinks are not thread-safe, and PostPlatformThreadTask aborts in this
// embedder build when called from a spawned renderer.
class ShuowenIpc {
 public:
  // Posted by the pipe reader thread; handled on the window thread.
  static constexpr UINT kPipeMessage = WM_APP + 0x51;

  ShuowenIpc(HWND window, flutter::FlutterEngine* engine);
  ~ShuowenIpc();

  ShuowenIpc(const ShuowenIpc&) = delete;
  ShuowenIpc& operator=(const ShuowenIpc&) = delete;

  void HandlePostedLine(const std::string& line);

 private:
  struct SharedState {
    std::mutex mutex;
    std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> sink;
    std::deque<std::string> pending;
    bool alive = true;
  };

  bool Connect(const std::string& pipe_name);
  bool Send(const std::string& line);
  void Disconnect();
  void ShowInactive(bool no_activate);
  void ShowHud();
  void HideWindow();
  void ReadLoop();
  void Emit(const std::string& line);
  void EmitOnPlatform(const std::string& line);

  HWND window_ = nullptr;
  flutter::FlutterEngine* engine_ = nullptr;

  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      method_channel_;
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>
      event_channel_;
  std::shared_ptr<SharedState> shared_;

  std::mutex send_mutex_;
  std::mutex pipe_mutex_;
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  std::atomic<bool> running_{false};
  std::thread reader_;
};

#endif  // RUNNER_SHUOWEN_IPC_H_
