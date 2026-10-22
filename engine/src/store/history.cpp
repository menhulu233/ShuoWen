#include "history.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <sstream>

#include "util/log.h"
#include "util/text.h"

namespace shuowen {
namespace {

std::string ReadFileUtf8(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return {};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool WriteFileUtf8Atomic(const std::filesystem::path& file,
                         const std::string& text) {
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  const auto tmp = file.wstring() + L".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) return false;
  }
  if (!MoveFileExW(tmp.c_str(), file.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

Json EntryToJson(const HistoryEntry& entry) {
  Json node = Json::Object();
  node.Set("Id", Json(entry.id));
  node.Set("TimestampMs", Json(entry.timestamp_ms));
  node.Set("Text", Json(entry.text));
  node.Set("Mode", Json(entry.mode));
  Json tags = Json::Array();
  for (const auto& tag : entry.tags) tags.Push(Json(tag));
  node.Set("Tags", std::move(tags));
  if (!entry.title.empty()) node.Set("Title", Json(entry.title));
  if (entry.expires_at_ms > 0) node.Set("ExpiresAtMs", Json(entry.expires_at_ms));
  return node;
}

HistoryEntry EntryFromJson(const Json& node) {
  HistoryEntry entry;
  if (const Json* v = node.Find("Id"); v && v->is_string()) entry.id = v->AsString();
  if (const Json* v = node.Find("TimestampMs"); v && v->is_number()) entry.timestamp_ms = v->AsInt64();
  if (const Json* v = node.Find("Text"); v && v->is_string()) entry.text = v->AsString();
  if (const Json* v = node.Find("Mode"); v && v->is_string()) entry.mode = v->AsString();
  if (const Json* v = node.Find("Title"); v && v->is_string()) entry.title = v->AsString();
  if (const Json* v = node.Find("ExpiresAtMs"); v && v->is_number()) entry.expires_at_ms = v->AsInt64();
  if (const Json* v = node.Find("Tags"); v && v->is_array()) {
    for (const auto& tag : v->AsArray()) {
      if (tag.is_string()) entry.tags.push_back(tag.AsString());
    }
  }
  if (entry.id.empty()) entry.id = WideToUtf8(NewUuid());
  if (entry.timestamp_ms == 0) entry.timestamp_ms = NowUnixMs();
  return entry;
}

}  // namespace

HistoryStore::HistoryStore(std::filesystem::path file) : file_(std::move(file)) {
  Load();
  debounce_ = std::thread([this] { DebounceLoop(); });
}

HistoryStore::~HistoryStore() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    if (dirty_) PersistLocked();
  }
  wake_.notify_all();
  if (debounce_.joinable()) debounce_.join();
}

void HistoryStore::Load() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
  lifetime_chars_ = 0;
  const std::string text = ReadFileUtf8(file_);
  if (text.empty()) return;

  std::string error;
  Json root = Json::Parse(text, &error);
  if (root.is_array()) {
    for (const auto& node : root.AsArray()) {
      if (!node.is_object()) continue;
      entries_.push_back(EntryFromJson(node));
      lifetime_chars_ += static_cast<int64_t>(
          Utf8ToWide(entries_.back().text).size());
    }
  } else if (root.is_object()) {
    if (const Json* v = root.Find("LifetimeChars"); v && v->is_number()) {
      lifetime_chars_ = v->AsInt64();
    }
    if (const Json* v = root.Find("Entries"); v && v->is_array()) {
      for (const auto& node : v->AsArray()) {
        if (node.is_object()) entries_.push_back(EntryFromJson(node));
      }
    }
  } else {
    LogF("history: parse failed (%s), starting empty", error.c_str());
    return;
  }
  PruneExpiredLocked();
  while (entries_.size() > HistoryStore::kMaxEntries) entries_.pop_front();
}

void HistoryStore::Flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (dirty_) PersistLocked();
}

void HistoryStore::PersistLocked() {
  Json root = Json::Object();
  root.Set("LifetimeChars", Json(lifetime_chars_));
  Json list = Json::Array();
  for (const auto& entry : entries_) list.Push(EntryToJson(entry));
  root.Set("Entries", std::move(list));
  if (!WriteFileUtf8Atomic(file_, root.Dump() + "\n")) {
    LogF("history: failed to save %s", WideToUtf8(file_.wstring()).c_str());
  } else {
    dirty_ = false;
  }
}

void HistoryStore::PruneExpiredLocked() {
  const int64_t now = NowUnixMs();
  const auto before = entries_.size();
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [now](const HistoryEntry& entry) {
                                  return entry.expires_at_ms > 0 &&
                                         entry.expires_at_ms <= now;
                                }),
                 entries_.end());
  if (entries_.size() != before) dirty_ = true;
}

void HistoryStore::MarkDirtyLocked() {
  dirty_ = true;
  wake_.notify_one();
}

void HistoryStore::DebounceLoop() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!stopping_) {
    wake_.wait_for(lock, std::chrono::milliseconds(2000),
                   [this] { return stopping_ || dirty_; });
    if (stopping_) break;
    if (dirty_) PersistLocked();
  }
}

HistoryEntry HistoryStore::Append(const std::string& text,
                                  const std::string& mode, bool ephemeral) {
  HistoryEntry entry;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    entry.id = WideToUtf8(NewUuid());
    entry.timestamp_ms = NowUnixMs();
    entry.text = TrimUtf8(text);
    entry.mode = mode;
    if (ephemeral) entry.expires_at_ms = entry.timestamp_ms + 60000;
    if (entry.text.empty()) return {};
    entries_.push_back(entry);
    if (entries_.size() > HistoryStore::kMaxEntries) entries_.pop_front();
    lifetime_chars_ += static_cast<int64_t>(Utf8ToWide(entry.text).size());
    MarkDirtyLocked();
  }
  return entry;
}

bool HistoryStore::Update(const std::string& id, const std::string& text,
                          const std::string& title,
                          const std::vector<std::string>& tags) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& entry : entries_) {
    if (entry.id != id) continue;
    entry.text = TrimUtf8(text);
    entry.title = TrimUtf8(title);
    entry.tags = tags;
    MarkDirtyLocked();
    return true;
  }
  return false;
}

bool HistoryStore::Remove(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto before = entries_.size();
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&id](const HistoryEntry& entry) {
                                  return entry.id == id;
                                }),
                 entries_.end());
  if (entries_.size() == before) return false;
  MarkDirtyLocked();
  return true;
}

void HistoryStore::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
  lifetime_chars_ = 0;
  MarkDirtyLocked();
}

std::vector<HistoryEntry> HistoryStore::List() {
  std::lock_guard<std::mutex> lock(mutex_);
  PruneExpiredLocked();
  std::vector<HistoryEntry> copy(entries_.begin(), entries_.end());
  std::sort(copy.begin(), copy.end(),
            [](const HistoryEntry& a, const HistoryEntry& b) {
              return a.timestamp_ms > b.timestamp_ms;
            });
  return copy;
}

int64_t HistoryStore::LifetimeChars() {
  std::lock_guard<std::mutex> lock(mutex_);
  return lifetime_chars_;
}

int64_t HistoryStore::Count() {
  std::lock_guard<std::mutex> lock(mutex_);
  PruneExpiredLocked();
  return static_cast<int64_t>(entries_.size());
}

}  // namespace shuowen
