#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace shuowen {

// Starts and tracks the Flutter renderer processes. The engine never embeds
// Flutter; each window is its own process, so a renderer crash or a closed
// settings window cannot interrupt dictation.
class Launcher {
 public:
  ~Launcher();

  bool Open(const std::string& window);
  bool EnsureHud();
  bool EnsureOnCall();
  bool IsRunning(const std::string& window) const;
  void ShutdownAll();

 private:
  struct Entry {
    void* process = nullptr;
    unsigned long pid = 0;
    std::string window;
  };

  bool Spawn(const std::string& window);
  void Reap();

  mutable std::mutex mutex_;
  std::vector<Entry> entries_;
};

}  // namespace shuowen
