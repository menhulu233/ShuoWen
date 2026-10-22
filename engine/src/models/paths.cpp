#include "paths.h"

#include <system_error>

#include "config.h"

namespace shuowen {
namespace {

std::wstring TierName(int tier_ms) {
  return L"chunk-" + std::to_wstring(tier_ms) + L"ms-model";
}

std::wstring FileName(int tier_ms, const wchar_t* prefix,
                      const wchar_t* suffix) {
  return std::wstring(prefix) + std::to_wstring(tier_ms) + suffix;
}

}  // namespace

int ModelPaths::NormalizeTier(int tier_ms) {
  switch (tier_ms) {
    case 160:
    case 480:
    case 960:
    case 1920:
      return tier_ms;
    default:
      return 960;
  }
}

ModelPaths ModelPaths::ForTier(int tier_ms) {
  ModelPaths paths;
  paths.tier_ms = NormalizeTier(tier_ms);
  paths.writable_root = paths::ModelsWritableRoot();
  paths.bundled_root = paths::BundledModelsRoot();
  return paths;
}

std::filesystem::path ModelPaths::Resolve(const std::wstring& relative) const {
  std::error_code ec;
  const auto writable = writable_root / relative;
  const auto writable_size = std::filesystem::file_size(writable, ec);
  if (!ec && writable_size > 0) return writable;
  const auto bundled = bundled_root / relative;
  const auto bundled_size = std::filesystem::file_size(bundled, ec);
  if (!ec && bundled_size > 0) return bundled;
  return writable;
}

std::filesystem::path ModelPaths::TierDir() const {
  return writable_root / TierName(tier_ms);
}

std::filesystem::path ModelPaths::Encoder() const {
  return Resolve(TierName(tier_ms) + L"\\" + FileName(tier_ms, L"encoder-", L"ms.onnx"));
}

std::filesystem::path ModelPaths::Decoder() const {
  return Resolve(TierName(tier_ms) + L"\\" + FileName(tier_ms, L"decoder-", L"ms.onnx"));
}

std::filesystem::path ModelPaths::Joiner() const {
  return Resolve(TierName(tier_ms) + L"\\" + FileName(tier_ms, L"joiner-", L"ms.onnx"));
}

std::filesystem::path ModelPaths::Tokens() const {
  return Resolve(TierName(tier_ms) + L"\\tokens.txt");
}

std::filesystem::path ModelPaths::SileroVad() const {
  std::error_code ec;
  const auto writable = writable_root / L"silero_vad.onnx";
  const auto writable_size = std::filesystem::file_size(writable, ec);
  if (!ec && writable_size >= kMinSileroVadBytes) return writable;
  const auto bundled = bundled_root / L"silero_vad.onnx";
  const auto bundled_size = std::filesystem::file_size(bundled, ec);
  if (!ec && bundled_size >= kMinSileroVadBytes) return bundled;
  // Nothing usable exists. Return the writable target so downloads repair it
  // instead of trying to modify an installation directory.
  return writable;
}

std::vector<std::filesystem::path> ModelPaths::RequiredAsrFiles() const {
  return {Encoder(), Decoder(), Joiner(), Tokens()};
}

bool ModelPaths::AsrPresent() const {
  std::error_code ec;
  for (const auto& file : RequiredAsrFiles()) {
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size == 0) return false;
  }
  return true;
}

bool ModelPaths::SileroPresent() const {
  std::error_code ec;
  const auto size = std::filesystem::file_size(SileroVad(), ec);
  return !ec && size >= kMinSileroVadBytes;
}

}  // namespace shuowen
