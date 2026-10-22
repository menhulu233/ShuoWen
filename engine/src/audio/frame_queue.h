#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace shuowen {

// Bounded frame handoff that drops the OLDEST frame on overflow. Mic capture
// must never block on a slow decoder: dropping the oldest audio keeps latency
// bounded and is what the C# reference implementation does.
class FrameQueue {
 public:
  explicit FrameQueue(size_t capacity) : capacity_(capacity) {}

  void Push(std::vector<float> frame) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_.size() >= capacity_) {
        queue_.pop_front();
        ++dropped_;
      }
      queue_.push_back(std::move(frame));
    }
    cv_.notify_one();
  }

  // Returns false when the queue is completed and empty.
  bool Pop(std::vector<float>& out, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
      return completed_ || !queue_.empty();
    });
    if (queue_.empty()) return !completed_;
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }

  void Complete() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      completed_ = true;
    }
    cv_.notify_all();
  }

  size_t Dropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
  }

 private:
  const size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::vector<float>> queue_;
  bool completed_ = false;
  size_t dropped_ = 0;
};

}  // namespace shuowen
