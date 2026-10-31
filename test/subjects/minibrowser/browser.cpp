#include "ipc.h"

#include <array>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <print>
#include <pthread.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
constexpr size_t kTabCount = 2;

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

void
ReapChildren(const std::array<pid_t, kTabCount> &children)
{
  for (const pid_t child : children) {
    if (child <= 0) {
      continue;
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
  }
}
} // namespace

int
main(int argc, char *argv[])
{
  ipc::SetProcessName("browser");

  if (argc < 2 || argc > 4) {
    ipc::Log(std::string{ "Usage: " } + argv[0] + " <path-to-content> [navigations=6] [slow_ms=0]");
    LogCounts();
    return 1;
  }

  int navigations = 6;
  int slowMs = 0;
  if ((argc > 2 && (!ParseNumber(argv[2], navigations) || navigations < 0)) ||
      (argc > 3 && (!ParseNumber(argv[3], slowMs) || slowMs < 0))) {
    ipc::Log(std::string{ "Usage: " } + argv[0] + " <path-to-content> [navigations=6] [slow_ms=0]");
    LogCounts();
    return 1;
  }

  const std::string contentPath{ argv[1] };
  const std::string slowMsText = std::to_string(slowMs);
  std::array<int, kTabCount> parentFds{ -1, -1 };
  std::array<pid_t, kTabCount> children{ -1, -1 };

  for (uint32_t tabId = 0; tabId < kTabCount; ++tabId) {
    std::array<int, 2> fds{ -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds.data()) < 0) {
      ipc::Log(std::string{ "socketpair failed: " } + std::strerror(errno));
      for (const int fd : parentFds) {
        if (fd >= 0) {
          close(fd);
        }
      }
      ReapChildren(children);
      LogCounts();
      return 1;
    }

    const std::string tabIdText = std::to_string(tabId);
    const std::string fdText = std::to_string(fds[1]);
    const pid_t child = fork();
    if (child < 0) {
      ipc::Log(std::string{ "fork failed: " } + std::strerror(errno));
      close(fds[0]);
      close(fds[1]);
      for (const int fd : parentFds) {
        if (fd >= 0) {
          close(fd);
        }
      }
      ReapChildren(children);
      LogCounts();
      return 1;
    }
    if (child == 0) {
      close(fds[0]);
      const int flags = fcntl(fds[1], F_GETFD);
      if (flags < 0 || fcntl(fds[1], F_SETFD, flags & ~FD_CLOEXEC) < 0) {
        perror("fcntl");
        _exit(127);
      }
      std::array<char *, 5> childArgs{ const_cast<char *>(contentPath.c_str()),
        const_cast<char *>(tabIdText.c_str()),
        const_cast<char *>(fdText.c_str()),
        const_cast<char *>(slowMsText.c_str()),
        nullptr };
      execv(contentPath.c_str(), childArgs.data());
      perror("execv");
      _exit(127);
    }

    close(fds[1]);
    parentFds[tabId] = fds[0];
    children[tabId] = child;
  }

  ipc::Channel<ipc::Message> fromContent;
  std::array<std::atomic<bool>, kTabCount> socketOpen{ true, true };
  std::array<std::mutex, kTabCount> fdWriteMutexes;
  auto WriteToTab = [&](uint32_t tabId, const ipc::Message &msg) {
    std::lock_guard lock{ fdWriteMutexes[tabId] };
    return ipc::WriteMsg(parentFds[tabId], msg);
  };
  std::thread ioThread{ [&]() {
    pthread_setname_np(pthread_self(), "ipc-io");
    std::array<pollfd, kTabCount> pollFds{};
    for (size_t i = 0; i < kTabCount; ++i) {
      pollFds[i] = pollfd{ .fd = parentFds[i], .events = POLLIN, .revents = 0 };
    }

    size_t openFds = kTabCount;
    auto closeTab = [&](size_t tabId) {
      if (pollFds[tabId].fd >= 0) {
        pollFds[tabId].fd = -1;
        --openFds;
        socketOpen[tabId].store(false);
        fromContent.Send(ipc::Message{ .type = ipc::MsgType::TabClosed, .tabId = static_cast<uint32_t>(tabId) });
      }
    };
    while (openFds > 0) {
      const int pollResult = poll(pollFds.data(), pollFds.size(), -1);
      if (pollResult < 0) {
        if (errno == EINTR) {
          continue;
        }
        ipc::Log(std::string{ "poll failed: " } + std::strerror(errno));
        for (size_t i = 0; i < kTabCount; ++i) {
          closeTab(i);
        }
        break;
      }

      for (size_t i = 0; i < kTabCount; ++i) {
        if (pollFds[i].fd < 0 || pollFds[i].revents == 0) {
          continue;
        }

        const short events = pollFds[i].revents;
        if ((events & POLLIN) != 0) {
          ipc::Message msg{};
          if (ipc::ReadMsg(pollFds[i].fd, msg)) {
            if (msg.type == ipc::MsgType::PostMessage) {
              if (msg.tabId < kTabCount) {
                WriteToTab(msg.tabId, msg); // #IPC_ROUTE_BP
              } else {
                ipc::Log("received PostMessage with invalid destination tab");
              }
            } else {
              fromContent.Send(msg);
            }
            continue;
          }
        }

        if ((events & (POLLHUP | POLLERR | POLLNVAL)) != 0 || (events & POLLIN) != 0) {
          closeTab(i);
        }
      }
    }
  } };

  auto HandleTabClosed = [&](const ipc::Message &msg) {
    ipc::Log(std::string{ "tab " } + std::to_string(msg.tabId) + " exited early");
    for (uint32_t tabId = 0; tabId < kTabCount; ++tabId) {
      if (socketOpen[tabId].load()) {
        const int64_t expectedPosts = (navigations / 2) + (navigations % 2);
        const ipc::Message shutdown{ .type = ipc::MsgType::Shutdown,
          .tabId = tabId,
          .seq = 0,
          .arg = tabId == 1 ? expectedPosts : 0,
          .result = 0 };
        WriteToTab(tabId, shutdown);
      }
    }
    ioThread.join();
    for (const int fd : parentFds) {
      if (fd >= 0) {
        close(fd);
      }
    }
    ReapChildren(children);
    LogCounts();
    return 1;
  };

  std::array<bool, kTabCount> gotHello{};
  size_t helloCount = 0;
  while (helloCount < kTabCount) {
    const ipc::Message msg = fromContent.Receive();
    if (msg.type == ipc::MsgType::TabClosed) {
      return HandleTabClosed(msg);
    }
    if (msg.type == ipc::MsgType::Hello && msg.tabId < kTabCount && !gotHello[msg.tabId]) {
      gotHello[msg.tabId] = true;
      ++helloCount;
    }
  }

  for (int i = 0; i < navigations; ++i) {
    const uint32_t tabId = static_cast<uint32_t>(i % static_cast<int>(kTabCount));
    const ipc::Message msg{ .type = ipc::MsgType::Navigate,
      .tabId = tabId,
      .seq = static_cast<uint64_t>(i),
      .arg = 10'000 + (static_cast<int64_t>(i) * 1'000),
      .result = 0 };
    if (!WriteToTab(tabId, msg)) {
      ipc::Log(std::string{ "failed to send navigation " } + std::to_string(i));
    }
  }

  std::vector<int64_t> results(static_cast<size_t>(navigations));
  int receivedResults = 0;
  while (receivedResults < navigations) {
    const ipc::Message msg = fromContent.Receive();
    if (msg.type == ipc::MsgType::TabClosed) {
      return HandleTabClosed(msg);
    }
    if (msg.type == ipc::MsgType::RenderDone && msg.seq < results.size()) {
      results[msg.seq] = msg.result; // #UI_GOT_RESULT_BP
      ++receivedResults;
    }
  }

  for (uint32_t tabId = 0; tabId < kTabCount; ++tabId) {
    const int64_t expectedPosts = (navigations / 2) + (navigations % 2);
    const ipc::Message msg{
      .type = ipc::MsgType::Shutdown, .tabId = tabId, .seq = 0, .arg = tabId == 1 ? expectedPosts : 0, .result = 0
    };
    if (!WriteToTab(tabId, msg)) {
      ipc::Log(std::string{ "failed to send shutdown to tab " } + std::to_string(tabId));
    }
  }

  ioThread.join();
  for (const int fd : parentFds) {
    if (fd >= 0) {
      close(fd);
    }
  }

  bool childrenSucceeded = true;
  for (size_t i = 0; i < kTabCount; ++i) {
    int status = 0;
    pid_t result = -1;
    do {
      result = waitpid(children[i], &status, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      ipc::Log(std::string{ "content tab " } + std::to_string(i) + " did not exit successfully");
      childrenSucceeded = false;
    }
  }

  if (childrenSucceeded) {
    for (int i = 0; i < navigations; ++i) {
      const int64_t n = 10'000 + static_cast<int64_t>(i) * 1'000;
      std::println("seq {} tab {} n {} primes {}", i, i % static_cast<int>(kTabCount), n, results[i]);
    }
  }

  LogCounts();
  return childrenSucceeded ? 0 : 1;
}
