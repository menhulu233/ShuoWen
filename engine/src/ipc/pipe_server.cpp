#include "pipe_server.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <vector>

#include "util/log.h"
#include "util/text.h"

namespace shuowen {
namespace {

constexpr size_t kMaxQueuedLines = 512;

}  // namespace

PipeServer::~PipeServer() { Stop(); }

bool PipeServer::Start(const std::wstring& pipe_name, LineHandler on_line,
                       ConnectHandler on_connect,
                       DisconnectHandler on_disconnect) {
  if (running_.load()) return true;
  pipe_name_ = pipe_name;
  on_line_ = std::move(on_line);
  on_connect_ = std::move(on_connect);
  on_disconnect_ = std::move(on_disconnect);
  running_.store(true);
  accept_thread_ = std::thread([this] { AcceptLoop(); });
  LogF("ipc: listening on %s", WideToUtf8(pipe_name_).c_str());
  return true;
}

void PipeServer::Stop() {
  if (!running_.exchange(false)) return;

  // Wake ConnectNamedPipe so the accept thread can observe running_ == false.
  HANDLE wake = CreateFileW(pipe_name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, 0, nullptr);
  if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);

  std::vector<std::shared_ptr<Client>> clients;
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients = clients_;
  }
  for (auto& client : clients) CloseClient(client);

  if (accept_thread_.joinable()) accept_thread_.join();

  for (auto& client : clients) {
    if (client->reader.joinable()) client->reader.join();
    if (client->writer.joinable()) client->writer.join();
  }

  std::lock_guard<std::mutex> lock(clients_mutex_);
  clients_.clear();
  Log("ipc: stopped");
}

void PipeServer::CloseClient(std::shared_ptr<Client>& client) {
  {
    std::lock_guard<std::mutex> lock(client->write_mutex);
    client->closing = true;
    client->outgoing.clear();
  }
  client->write_cv.notify_all();
  HANDLE pipe = static_cast<HANDLE>(client->pipe.exchange(nullptr));
  if (pipe) {
    CancelIoEx(pipe, nullptr);
    CloseHandle(pipe);
  }
}

void PipeServer::AcceptLoop() {
  while (running_.load()) {
    HANDLE pipe = CreateNamedPipeW(
        pipe_name_.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
      LogF("ipc: CreateNamedPipe failed (%lu)", GetLastError());
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    const BOOL connected = ConnectNamedPipe(pipe, nullptr);
    const DWORD error = connected ? 0 : GetLastError();
    if (!running_.load()) {
      CloseHandle(pipe);
      break;
    }
    if (!connected && error != ERROR_PIPE_CONNECTED) {
      CloseHandle(pipe);
      continue;
    }

    auto client = std::make_shared<Client>();
    client->id = next_id_.fetch_add(1);
    client->pipe.store(pipe);
    {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      clients_.push_back(client);
    }
    LogF("ipc: client %llu connected",
         static_cast<unsigned long long>(client->id));
    if (on_connect_) on_connect_(client->id);
    client->reader = std::thread([this, client] { ReadLoop(client); });
    client->writer = std::thread([this, client] { WriteLoop(client); });
    LogF("ipc: client %llu threads started pipe=%p",
         static_cast<unsigned long long>(client->id), client->pipe.load());
  }
}

void PipeServer::ReadLoop(std::shared_ptr<Client> client) {
  char buffer[8192];
  while (running_.load()) {
    HANDLE pipe = static_cast<HANDLE>(client->pipe.load());
    if (!pipe) break;

    // Never leave a blocking ReadFile pending on this handle: a synchronous
    // WriteFile from the writer thread otherwise queues behind it and the
    // response never reaches the UI until the client disconnects.
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
      LogF("ipc: client %llu peek failed (%lu)",
           static_cast<unsigned long long>(client->id), GetLastError());
      break;
    }
    if (available == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    DWORD read = 0;
    const DWORD chunk = std::min<DWORD>(available, sizeof(buffer));
    if (!ReadFile(pipe, buffer, chunk, &read, nullptr) || read == 0) {
      LogF("ipc: client %llu read failed (%lu)",
           static_cast<unsigned long long>(client->id), GetLastError());
      break;
    }
    client->pending.append(buffer, read);
    size_t newline = 0;
    while ((newline = client->pending.find('\n')) != std::string::npos) {
      std::string line = client->pending.substr(0, newline);
      client->pending.erase(0, newline + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty()) continue;
      std::string response;
      if (on_line_) response = on_line_(client->id, line);
      if (!response.empty()) Enqueue(*client, response);
    }
  }

  const uint64_t id = client->id;
  {
    std::lock_guard<std::mutex> lock(client->write_mutex);
    client->closing = true;
  }
  client->write_cv.notify_all();
  HANDLE pipe = static_cast<HANDLE>(client->pipe.exchange(nullptr));
  if (pipe) {
    CancelIoEx(pipe, nullptr);
    CloseHandle(pipe);
  }

  RemoveClient(id);
  if (on_disconnect_) on_disconnect_(id);
  LogF("ipc: client %llu disconnected", static_cast<unsigned long long>(id));
}
void PipeServer::WriteLoop(std::shared_ptr<Client> client) {
  while (true) {
    std::string line;
    {
      std::unique_lock<std::mutex> lock(client->write_mutex);
      client->write_cv.wait(lock, [&client] {
        return client->closing || !client->outgoing.empty();
      });
      if (client->closing && client->outgoing.empty()) break;
      line = std::move(client->outgoing.front());
      client->outgoing.pop_front();
    }
    const bool wrote = WriteAll(*client, line + "\n");
    if (!wrote) {
      LogF("ipc: client %llu write failed (%lu)",
           static_cast<unsigned long long>(client->id), GetLastError());
      {
        std::lock_guard<std::mutex> lock(client->write_mutex);
        client->closing = true;
        client->outgoing.clear();
      }
      HANDLE pipe = static_cast<HANDLE>(client->pipe.exchange(nullptr));
      if (pipe) {
        CancelIoEx(pipe, nullptr);
        CloseHandle(pipe);
      }
      break;
    }
  }
}

bool PipeServer::WriteAll(Client& client, const std::string& payload) {
  HANDLE pipe = static_cast<HANDLE>(client.pipe.load());
  if (!pipe) return false;
  size_t offset = 0;
  while (offset < payload.size()) {
    DWORD written = 0;
    const DWORD chunk = static_cast<DWORD>(
        std::min<size_t>(payload.size() - offset, 32 * 1024));
    if (!WriteFile(pipe, payload.data() + offset, chunk, &written, nullptr) ||
        written == 0) {
      return false;
    }
    offset += written;
  }
  return true;
}

bool PipeServer::Enqueue(Client& client, const std::string& line) {
  {
    std::lock_guard<std::mutex> lock(client.write_mutex);
    if (client.closing || !client.pipe.load()) return false;
    if (client.outgoing.size() >= kMaxQueuedLines) {
      client.outgoing.pop_front();
    }
    client.outgoing.push_back(line);
  }
  client.write_cv.notify_one();
  return true;
}

void PipeServer::RemoveClient(uint64_t id) {
  std::lock_guard<std::mutex> lock(clients_mutex_);
  clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                [id](const std::shared_ptr<Client>& client) {
                                  return client->id == id;
                                }),
                 clients_.end());
}

void PipeServer::Broadcast(const std::string& line) {
  std::vector<std::shared_ptr<Client>> snapshot;
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    snapshot = clients_;
  }
  for (auto& client : snapshot) Enqueue(*client, line);
}

void PipeServer::SendTo(uint64_t client_id, const std::string& line) {
  std::shared_ptr<Client> target;
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& client : clients_) {
      if (client->id == client_id) {
        target = client;
        break;
      }
    }
  }
  if (target) Enqueue(*target, line);
}

size_t PipeServer::ClientCount() const {
  std::lock_guard<std::mutex> lock(clients_mutex_);
  return clients_.size();
}

}  // namespace shuowen
