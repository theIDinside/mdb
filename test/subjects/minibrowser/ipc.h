#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>

namespace ipc {
enum class MsgType : uint32_t
{
  Hello,
  Navigate,
  RenderDone,
  Shutdown,
  TabClosed, // parent-internal: ipc-io -> main when a tab's socket hits EOF; never on the wire
  PostMessage,
};

struct Message
{
  MsgType type;
  uint32_t tabId;
  uint64_t seq;
  int64_t arg;
  int64_t result;
};

template <typename T> class Channel
{
public:
  void Send(T value);
  T Receive();

private:
  std::mutex mMutex;
  std::condition_variable mCond;
  std::deque<T> mQueue;
};

extern template class Channel<Message>;

bool WriteMsg(int fd, const Message &msg);
bool ReadMsg(int fd, Message &msg);

void SetProcessName(std::string_view name);
void Log(std::string_view text);

extern std::string gProcessName;
extern std::atomic<uint64_t> gMessagesSent;
extern std::atomic<uint64_t> gMessagesReceived;
} // namespace ipc
