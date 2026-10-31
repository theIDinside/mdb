#include "ipc.h"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <pthread.h>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>

namespace {
void
LogCounts()
{
  ipc::Log(std::string{ "sent " } + std::to_string(ipc::gMessagesSent.load()) + " received " +
           std::to_string(ipc::gMessagesReceived.load()));
}

template <typename T>
bool
ParseNumber(std::string_view text, T &value)
{
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size();
}
} // namespace

int64_t
CountPrimes(int64_t n)
{
  int64_t count = 0;
  for (int64_t candidate = 2; candidate < n; ++candidate) {
    bool isPrime = true;
    for (int64_t divisor = 2; divisor <= candidate / divisor; ++divisor) {
      if (candidate % divisor == 0) {
        isPrime = false;
        break;
      }
    }
    if (isPrime) {
      ++count;
    }
  }
  return count;
}

int
main(int argc, char *argv[])
{
  ipc::SetProcessName(argc > 1 ? std::string{ "tab " } + argv[1] : "tab ?");

  const std::string usage = std::string{ "Usage: " } + argv[0] + " <tabId> <fd> [slow_ms=0]";
  uint32_t tabId = 0;
  int fd = -1;
  int slowMs = 0;
  if ((argc != 3 && argc != 4) || !ParseNumber(argc > 1 ? argv[1] : "", tabId) || tabId > 1 ||
      !ParseNumber(argc > 2 ? argv[2] : "", fd) || fd < 0 ||
      (argc == 4 && (!ParseNumber(argv[3], slowMs) || slowMs < 0))) {
    ipc::Log(usage);
    LogCounts();
    return 1;
  }

  ipc::Channel<ipc::Message> jobs;
  ipc::Channel<ipc::Message> results;
  std::thread worker{ [&]() {
    pthread_setname_np(pthread_self(), "worker");
    while (true) {
      ipc::Message msg = jobs.Receive();
      if (msg.type == ipc::MsgType::Shutdown) {
        return;
      }
      msg.result = CountPrimes(msg.arg); // #WORKER_COMPUTE_BP
      if (slowMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ slowMs });
      }
      msg.type = ipc::MsgType::RenderDone;
      results.Send(msg);
    }
  } };

  int exitCode = 0;
  uint64_t postMessageCount = 0;
  const ipc::Message hello{ ipc::MsgType::Hello, tabId, 0, 0, 0 };
  if (!ipc::WriteMsg(fd, hello)) {
    ipc::Log("failed to send Hello");
    exitCode = 1;
  } else {
    while (true) {
      ipc::Message msg{};
      if (!ipc::ReadMsg(fd, msg)) {
        break;
      }
      if (msg.type == ipc::MsgType::Navigate) {
        jobs.Send(msg);
        const ipc::Message reply = results.Receive();
        if (tabId == 0) {
          const ipc::Message postMessage{
            .type = ipc::MsgType::PostMessage, .tabId = 1, .seq = reply.seq, .arg = reply.result, .result = 0
          };
          if (!ipc::WriteMsg(fd, postMessage)) {
            ipc::Log("failed to send PostMessage");
            exitCode = 1;
            break;
          }
        }
        if (!ipc::WriteMsg(fd, reply)) { // #CONTENT_FORWARD_BP
          ipc::Log("failed to send RenderDone");
          exitCode = 1;
          break;
        }
      } else if (msg.type == ipc::MsgType::Shutdown) {
        if (tabId == 1) {
          exitCode = msg.arg >= 0 && postMessageCount == static_cast<uint64_t>(msg.arg) ? 0 : 2;
        }
        break;
      } else if (msg.type == ipc::MsgType::PostMessage && tabId == 1) {
        ++postMessageCount;
        ipc::Log(std::string{ "received PostMessage payload " } + std::to_string(msg.arg));
      } else {
        ipc::Log("received an unexpected message");
        exitCode = 1;
        break;
      }
    }
  }

  jobs.Send(ipc::Message{ .type = ipc::MsgType::Shutdown });
  worker.join();
  close(fd);
  LogCounts();
  return exitCode;
}
