#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace shuowen {

// Multi-client NDJSON server over a Windows named pipe. Each client has a
// reader thread and a bounded writer queue, so a renderer that stops draining
// its pipe can never stall the engine or another window.
class PipeServer {
 public:
  using LineHandler =
      std::function<std::string(uint64_t client_id, const std::string& line)>;
  using ConnectHandler = std::function<void(uint64_t client_id)>;
  using DisconnectHandler = std::function<void(uint64_t client_id)>;

  PipeServer() = default;
  ~PipeServer();

  bool Start(const std::wstring& pipe_name, LineHandler on_line,
             ConnectHandler on_connect, DisconnectHandler on_disconnect);
  void Stop();

  void Broadcast(const std::string& line);
  void SendTo(uint64_t client_id, const std::string& line);
  size_t ClientCount() const;

 private:
  struct Client {
    uint64_t id = 0;
    std::atomic<void*> pipe{nullptr};

    std::mutex write_mutex;
    std::condition_variable write_cv;
    std::deque<std::string> outgoing;
    bool closing = false;

    std::thread reader;
    std::thread writer;
    std::string pending;

    ~Client() {
      // A thread object must never be destroyed while joinable. A client can
      // be released on its own reader/writer thread; detaching here is safe
      // and keeps the process alive on teardown paths.
      if (reader.joinable()) reader.detach();
      if (writer.joinable()) writer.detach();
    }
  };

  void AcceptLoop();
  void ReadLoop(std::shared_ptr<Client> client);
  void WriteLoop(std::shared_ptr<Client> client);
  bool Enqueue(Client& client, const std::string& line);
  bool WriteAll(Client& client, const std::string& payload);
  void RemoveClient(uint64_t id);
  void CloseClient(std::shared_ptr<Client>& client);

  std::wstring pipe_name_;
  LineHandler on_line_;
  ConnectHandler on_connect_;
  DisconnectHandler on_disconnect_;

  mutable std::mutex clients_mutex_;
  std::vector<std::shared_ptr<Client>> clients_;
  std::thread accept_thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> next_id_{1};
};

}  // namespace shuowen
