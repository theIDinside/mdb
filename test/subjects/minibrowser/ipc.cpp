#include "ipc.h"

#include <cerrno>
#include <cstdio>
#include <format>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace ipc {
std::string gProcessName;
std::atomic<uint64_t> gMessagesSent = 0;
std::atomic<uint64_t> gMessagesReceived = 0;

template <typename T>
void
Channel<T>::Send(T value)
{
  {
    std::lock_guard lock{ mMutex };
    mQueue.push_back(std::move(value));
  }
  mCond.notify_one();
}

template <typename T>
T
Channel<T>::Receive()
{
  std::unique_lock lock{ mMutex };
  mCond.wait(lock, [this]() { return !mQueue.empty(); });
  T value = std::move(mQueue.front());
  mQueue.pop_front();
  return value;
}

template class Channel<Message>;

bool
WriteMsg(int fd, const Message &msg)
{
  const auto *data = reinterpret_cast<const char *>(&msg);
  size_t written = 0;
  while (written < sizeof(msg)) {
    const ssize_t result = send(fd, data + written, sizeof(msg) - written, MSG_NOSIGNAL);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (result == 0) {
      return false;
    }
    written += static_cast<size_t>(result);
  }
  ++gMessagesSent; // #IPC_WRITE_BP
  return true;
}

bool
ReadMsg(int fd, Message &msg)
{
  auto *data = reinterpret_cast<char *>(&msg);
  size_t received = 0;
  while (received < sizeof(msg)) {
    const ssize_t result = read(fd, data + received, sizeof(msg) - received);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (result == 0) {
      return false;
    }
    received += static_cast<size_t>(result);
  }
  ++gMessagesReceived;
  return true;
}

void
SetProcessName(std::string_view name)
{
  gProcessName = name;
}

void
Log(std::string_view text)
{
  // One write per line, so lines from different processes don't interleave.
  const std::string line = std::format("[{}] {}\n", gProcessName, text);
  fwrite(line.data(), 1, line.size(), stderr);
}
} // namespace ipc
