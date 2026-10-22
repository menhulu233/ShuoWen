#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "util/json.h"

namespace shuowen {

struct HistoryEntry {
  std::string id;
  int64_t timestamp_ms = 0;
  std::string text;
  std::string mode = "paste";
  std::vector<std::string> tags;
  std::string title;
  int64_t expires_at_ms = 0;
};

class HistoryStore {
 public:
  // Older entries are dropped once this cap is reached. LifetimeChars is
  // intentionally cumulative and does not decrease when entries are pruned.
  static constexpr std::size_t kMaxEntries = 10000;

  explicit HistoryStore(std::filesystem::path file);
  ~HistoryStore();

  HistoryStore(const HistoryStore&) = delete;
  HistoryStore& operator=(const HistoryStore&) = delete;

  void Load();
  void Flush();

  HistoryEntry Append(const std::string& text, const std::string& mode,
                      bool ephemeral);
  bool Update(const std::string& id, const std::string& text,
              const std::string& title, const std::vector<std::string>& tags);
  bool Remove(const std::string& id);
  void Clear();

  std::vector<HistoryEntry> List();
  int64_t LifetimeChars();
  int64_t Count();

 private:
  void MarkDirtyLocked();
  void PersistLocked();
  void PruneExpiredLocked();
  void DebounceLoop();

  std::mutex mutex_;
  std::filesystem::path file_;
  std::deque<HistoryEntry> entries_;
  int64_t lifetime_chars_ = 0;
  bool dirty_ = false;
  bool stopping_ = false;
  std::condition_variable wake_;
  std::thread debounce_;
};

}  // namespace shuowen
