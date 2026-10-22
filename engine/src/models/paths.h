#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace shuowen {

struct ModelPaths {
  // sherpa-onnx 1.13.x supports the V4 Silero graph (~1.8 MB). The
  // unversioned V5 asset is ~640 KB and must never be treated as usable.
  static constexpr std::uintmax_t kMinSileroVadBytes = 1000000;

  int tier_ms = 960;
  std::filesystem::path writable_root;
  std::filesystem::path bundled_root;

  static ModelPaths ForTier(int tier_ms);
  static int NormalizeTier(int tier_ms);

  std::filesystem::path Resolve(const std::wstring& relative) const;
  std::filesystem::path TierDir() const;
  std::filesystem::path Encoder() const;
  std::filesystem::path Decoder() const;
  std::filesystem::path Joiner() const;
  std::filesystem::path Tokens() const;
  std::filesystem::path SileroVad() const;

  std::vector<std::filesystem::path> RequiredAsrFiles() const;
  bool AsrPresent() const;
  bool SileroPresent() const;
};

}  // namespace shuowen
