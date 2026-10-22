#include "cue.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace shuowen {
namespace {

constexpr int kRate = 44100;
constexpr double kPi = 3.14159265358979323846;

std::mutex g_mutex;
std::vector<char> g_buffer;

double VolumeScale(const std::string& volume) {
  if (volume == "high") return 0.55;
  if (volume == "med" || volume == "medium") return 0.32;
  return 0.18;
}

void AppendTone(std::vector<float>& samples, double frequency, double seconds,
                double amplitude) {
  const int count = static_cast<int>(seconds * kRate);
  const int attack = std::min(count / 8, static_cast<int>(kRate * 0.008));
  for (int i = 0; i < count; ++i) {
    const double t = static_cast<double>(i) / kRate;
    double envelope = std::exp(-5.0 * t / seconds);
    if (i < attack) envelope *= static_cast<double>(i) / attack;
    const double fade = static_cast<double>(count - i) / count;
    samples.push_back(static_cast<float>(
        std::sin(2.0 * kPi * frequency * t) * amplitude * envelope * fade));
  }
}

std::vector<float> BuildSound(bool start, const std::string& theme) {
  std::vector<float> samples;
  if (theme == "tick") {
    AppendTone(samples, start ? 1350.0 : 900.0, 0.045, 0.8);
    AppendTone(samples, start ? 1800.0 : 1200.0, 0.035, 0.4);
  } else if (theme == "soft") {
    AppendTone(samples, start ? 620.0 : 520.0, 0.18, 0.5);
    AppendTone(samples, start ? 930.0 : 780.0, 0.16, 0.25);
  } else if (theme == "drop") {
    AppendTone(samples, start ? 900.0 : 700.0, 0.10, 0.5);
    AppendTone(samples, start ? 620.0 : 480.0, 0.16, 0.35);
  } else if (theme == "marimba") {
    AppendTone(samples, start ? 523.25 : 392.0, 0.16, 0.55);
    AppendTone(samples, start ? 783.99 : 587.33, 0.18, 0.32);
    AppendTone(samples, start ? 1046.5 : 783.99, 0.12, 0.16);
  } else {  // chime (default)
    AppendTone(samples, start ? 880.0 : 660.0, 0.14, 0.45);
    AppendTone(samples, start ? 1318.5 : 990.0, 0.20, 0.28);
  }
  return samples;
}

std::vector<char> BuildWav(const std::vector<float>& samples,
                           double amplitude_scale) {
  const uint32_t data_bytes =
      static_cast<uint32_t>(samples.size() * sizeof(int16_t));
  std::vector<char> wav(44 + data_bytes, 0);
  auto put32 = [&](size_t offset, uint32_t value) {
    wav[offset + 0] = static_cast<char>(value & 0xFF);
    wav[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    wav[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
    wav[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
  };
  auto put16 = [&](size_t offset, uint16_t value) {
    wav[offset + 0] = static_cast<char>(value & 0xFF);
    wav[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
  };
  std::memcpy(wav.data(), "RIFF", 4);
  put32(4, static_cast<uint32_t>(wav.size() - 8));
  std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
  put32(16, 16);
  put16(20, 1);  // PCM
  put16(22, 1);  // mono
  put32(24, kRate);
  put32(28, kRate * 2);
  put16(32, 2);
  put16(34, 16);
  std::memcpy(wav.data() + 36, "data", 4);
  put32(40, data_bytes);
  for (size_t i = 0; i < samples.size(); ++i) {
    double value = samples[i] * amplitude_scale;
    value = std::max(-1.0, std::min(1.0, value));
    const auto pcm = static_cast<int16_t>(value * 32767.0);
    put16(44 + i * 2, static_cast<uint16_t>(pcm));
  }
  return wav;
}

}  // namespace

void Cue::Play(bool start, const std::string& theme,
               const std::string& volume) {
  const auto samples = BuildSound(start, theme);
  const double scale = VolumeScale(volume);
  auto wav = BuildWav(samples, scale);

  std::lock_guard<std::mutex> lock(g_mutex);
  g_buffer = std::move(wav);
  PlaySoundW(reinterpret_cast<LPCWSTR>(g_buffer.data()), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

}  // namespace shuowen
