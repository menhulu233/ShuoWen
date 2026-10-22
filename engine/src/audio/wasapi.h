#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace shuowen {

// WASAPI shared-mode capture delivering 16 kHz mono float frames. The engine
// asks WASAPI for the target format with AUTOCONVERTPCM; if the endpoint
// rejects that, it falls back to the mix format and resamples linearly.
class WasapiCapture {
 public:
  using FrameCallback = std::function<void(const float*, size_t)>;
  using ErrorCallback = std::function<void(const std::string&)>;

  WasapiCapture() = default;
  ~WasapiCapture();

  WasapiCapture(const WasapiCapture&) = delete;
  WasapiCapture& operator=(const WasapiCapture&) = delete;

  static std::vector<std::pair<std::string, std::string>> Devices();

  bool Start(const std::string& device_id, FrameCallback on_frame,
             ErrorCallback on_error);
  void Stop();

  bool running() const { return running_.load(); }
  std::string device_name() const { return device_name_; }

 private:
  void ThreadMain(std::string device_id, FrameCallback on_frame,
                  ErrorCallback on_error,
                  std::shared_ptr<std::promise<bool>> initialized);

  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
  std::thread thread_;
  std::string device_name_;
};

}  // namespace shuowen
