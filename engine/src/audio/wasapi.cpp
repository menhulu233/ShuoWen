#include "wasapi.h"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <propsys.h>
#include <wrl/client.h>

#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <vector>

#include "util/log.h"
#include "util/text.h"

using Microsoft::WRL::ComPtr;

namespace shuowen {
namespace {

constexpr int kTargetRate = 16000;

std::wstring FriendlyName(IMMDevice* device) {
  ComPtr<IPropertyStore> store;
  if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) return {};
  PROPVARIANT value;
  PropVariantInit(&value);
  std::wstring name;
  if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) &&
      value.vt == VT_LPWSTR && value.pwszVal) {
    name = value.pwszVal;
  }
  PropVariantClear(&value);
  return name;
}

bool IsFloatFormat(const WAVEFORMATEX* format) {
  if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
  if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
      format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
  }
  return false;
}

class LinearResampler {
 public:
  explicit LinearResampler(int source_rate)
      : step_(static_cast<double>(source_rate) / kTargetRate) {}

  void Push(float sample) { buffer_.push_back(sample); }

  void Drain(std::vector<float>& out) {
    while (position_ + 1.0 < static_cast<double>(buffer_.size())) {
      const size_t index = static_cast<size_t>(position_);
      const double fraction = position_ - static_cast<double>(index);
      out.push_back(static_cast<float>(buffer_[index] * (1.0 - fraction) +
                                       buffer_[index + 1] * fraction));
      position_ += step_;
    }
    const size_t consumed = static_cast<size_t>(position_);
    if (consumed > 8192) {
      buffer_.erase(buffer_.begin(),
                    buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
      position_ -= static_cast<double>(consumed);
    }
  }

 private:
  double step_;
  double position_ = 0.0;
  std::vector<float> buffer_;
};

}  // namespace

WasapiCapture::~WasapiCapture() { Stop(); }

void WasapiCapture::Stop() {
  stop_.store(true);
  if (thread_.joinable()) thread_.join();
  running_.store(false);
}

bool WasapiCapture::Start(const std::string& device_id, FrameCallback on_frame,
                          ErrorCallback on_error) {
  if (running_.load()) return true;
  if (thread_.joinable()) thread_.join();
  stop_.store(false);
  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();
  thread_ = std::thread(
      [this, device_id, on_frame, on_error, promise] {
        ThreadMain(device_id, on_frame, on_error, promise);
      });
  if (future.wait_for(std::chrono::seconds(8)) != std::future_status::ready) {
    stop_.store(true);
    return false;
  }
  return future.get();
}

void WasapiCapture::ThreadMain(
    std::string device_id, FrameCallback on_frame, ErrorCallback on_error,
    std::shared_ptr<std::promise<bool>> initialized) {
  bool signaled = false;
  auto signal = [&](bool value) {
    if (signaled) return;
    signaled = true;
    try {
      initialized->set_value(value);
    } catch (...) {
    }
  };

  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool com_owned = SUCCEEDED(com);
  auto fail = [&](const std::string& message) {
    LogF("mic: %s", message.c_str());
    if (on_error) on_error(message);
    running_.store(false);
    signal(false);
    if (com_owned) CoUninitialize();
  };

  ComPtr<IMMDeviceEnumerator> enumerator;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
  if (FAILED(hr)) {
    fail("cannot create audio device enumerator");
    return;
  }

  ComPtr<IMMDevice> device;
  if (!device_id.empty()) {
    const std::wstring id = Utf8ToWide(device_id);
    hr = enumerator->GetDevice(id.c_str(), &device);
  }
  if (!device) {
    hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
  }
  if (!device) {
    hr = enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &device);
  }
  if (!device) {
    fail("no microphone found");
    return;
  }

  device_name_ = WideToUtf8(FriendlyName(device.Get()));
  LogF("mic: using \"%s\"", device_name_.c_str());

  ComPtr<IAudioClient> client;
  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
  if (FAILED(hr)) {
    fail("cannot activate IAudioClient");
    return;
  }

  WAVEFORMATEX target{};
  target.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
  target.nChannels = 1;
  target.nSamplesPerSec = kTargetRate;
  target.wBitsPerSample = 32;
  target.nBlockAlign = 4;
  target.nAvgBytesPerSec = kTargetRate * 4;
  target.cbSize = 0;

  const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                      AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                      AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
  hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 300000, 0, &target,
                          nullptr);

  bool direct = SUCCEEDED(hr);
  WAVEFORMATEX* mix_format = nullptr;
  std::unique_ptr<LinearResampler> resampler;
  if (!direct) {
    LogF("mic: AUTOCONVERTPCM unavailable (0x%08lx); using mix format",
         static_cast<unsigned long>(hr));
    hr = client->GetMixFormat(&mix_format);
    if (FAILED(hr) || !mix_format) {
      fail("cannot query the microphone mix format");
      return;
    }
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 300000, 0,
                            mix_format, nullptr);
    if (FAILED(hr)) {
      CoTaskMemFree(mix_format);
      fail("cannot initialize WASAPI capture");
      return;
    }
    resampler = std::make_unique<LinearResampler>(mix_format->nSamplesPerSec);
    LogF("mic: mix format %u Hz %u ch %u bit%s",
         mix_format->nSamplesPerSec, mix_format->nChannels,
         mix_format->wBitsPerSample, IsFloatFormat(mix_format) ? " float" : "");
  }

  ComPtr<IAudioCaptureClient> capture;
  hr = client->GetService(IID_PPV_ARGS(&capture));
  if (FAILED(hr)) {
    CoTaskMemFree(mix_format);
    fail("cannot get IAudioCaptureClient");
    return;
  }

  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) {
    CoTaskMemFree(mix_format);
    fail("cannot create capture event");
    return;
  }
  hr = client->SetEventHandle(event);
  if (FAILED(hr)) {
    CloseHandle(event);
    CoTaskMemFree(mix_format);
    fail("cannot set capture event");
    return;
  }
  hr = client->Start();
  if (FAILED(hr)) {
    CloseHandle(event);
    CoTaskMemFree(mix_format);
    fail("cannot start WASAPI capture");
    return;
  }

  running_.store(true);
  signal(true);
  Log("mic: capture started");

  while (!stop_.load()) {
    const DWORD wait = WaitForSingleObject(event, 100);
    if (stop_.load()) break;
    if (wait != WAIT_OBJECT_0) continue;

    UINT32 packet = 0;
    while (!stop_.load() && SUCCEEDED(capture->GetNextPacketSize(&packet)) &&
           packet > 0) {
      BYTE* data = nullptr;
      UINT32 frames = 0;
      DWORD buffer_flags = 0;
      if (FAILED(capture->GetBuffer(&data, &frames, &buffer_flags, nullptr,
                                    nullptr))) {
        break;
      }

      if (direct) {
        if (frames > 0) {
          if (buffer_flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            std::vector<float> zeros(frames, 0.0f);
            on_frame(zeros.data(), zeros.size());
          } else {
            on_frame(reinterpret_cast<const float*>(data), frames);
          }
        }
      } else if (frames > 0) {
        std::vector<float> mono;
        mono.reserve(frames);
        const bool is_float = IsFloatFormat(mix_format);
        const int channels = mix_format->nChannels;
        if (buffer_flags & AUDCLNT_BUFFERFLAGS_SILENT) {
          mono.assign(frames, 0.0f);
        } else if (is_float) {
          const auto* samples = reinterpret_cast<const float*>(data);
          for (UINT32 i = 0; i < frames; ++i) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) sum += samples[i * channels + c];
            mono.push_back(sum / static_cast<float>(channels));
          }
        } else if (mix_format->wBitsPerSample == 16) {
          const auto* samples = reinterpret_cast<const int16_t*>(data);
          for (UINT32 i = 0; i < frames; ++i) {
            int sum = 0;
            for (int c = 0; c < channels; ++c) sum += samples[i * channels + c];
            mono.push_back(static_cast<float>(sum) /
                           (static_cast<float>(channels) * 32768.0f));
          }
        }
        std::vector<float> resampled;
        resampled.reserve(frames);
        for (float sample : mono) resampler->Push(sample);
        resampler->Drain(resampled);
        if (!resampled.empty()) on_frame(resampled.data(), resampled.size());
      }
      capture->ReleaseBuffer(frames);
    }
  }

  client->Stop();
  CloseHandle(event);
  CoTaskMemFree(mix_format);
  if (com_owned) CoUninitialize();
  running_.store(false);
  Log("mic: capture stopped");
}

std::vector<std::pair<std::string, std::string>> WasapiCapture::Devices() {
  std::vector<std::pair<std::string, std::string>> result;
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ComPtr<IMMDeviceEnumerator> enumerator;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                 CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
    ComPtr<IMMDeviceCollection> collection;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE,
                                                 &collection))) {
      UINT32 count = 0;
      collection->GetCount(&count);
      for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device))) continue;
        LPWSTR id = nullptr;
        if (FAILED(device->GetId(&id))) continue;
        result.emplace_back(WideToUtf8(id), WideToUtf8(FriendlyName(device.Get())));
        CoTaskMemFree(id);
      }
    }
  }
  if (SUCCEEDED(com)) CoUninitialize();
  return result;
}

}  // namespace shuowen
