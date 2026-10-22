#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "models/paths.h"

namespace shuowen {

struct DownloadProgress {
  std::string file;
  int64_t received = 0;
  int64_t total = 0;  // 0 = unknown
  int index = 0;
  int count = 1;
};

using DownloadProgressFn = std::function<void(const DownloadProgress&)>;

bool HttpDownload(const std::string& url, const std::filesystem::path& dest,
                  const DownloadProgressFn& progress,
                  std::atomic<bool>& cancel, std::string* error);

bool EnsureTier(const ModelPaths& paths, const std::string& source,
                const DownloadProgressFn& progress, std::atomic<bool>& cancel,
                std::string* error);

bool EnsureVad(const ModelPaths& paths, const DownloadProgressFn& progress,
               std::atomic<bool>& cancel, std::string* error);

}  // namespace shuowen
