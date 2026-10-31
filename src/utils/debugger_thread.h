/** LICENSE TEMPLATE */
#pragma once
#include <functional>
#include <thread>

namespace mdb {
class DebuggerThread
{
public:
  using SpawnFunction = std::function<void(std::stop_token &)>;

private:
  static int
  GetNextDebuggerThreadNumber()
  {
    static int i = 0;
    return i++;
  }

  std::string mThreadName;
  explicit DebuggerThread(std::string &&name, SpawnFunction &&task) noexcept;

public:
  static void BlockSigchildMustSucceed() noexcept;
  using OwnedPtr = std::unique_ptr<DebuggerThread>;

  ~DebuggerThread() noexcept;
  /// Create a debugger thread
  static std::unique_ptr<DebuggerThread> SpawnDebuggerThread(SpawnFunction &&task) noexcept;
  static std::unique_ptr<DebuggerThread> SpawnDebuggerThread(
    std::string threadName, SpawnFunction &&task) noexcept;

  /// Start the thread.
  void Start() noexcept;
  /// Join the thread.
  void Join() noexcept;
  /// Check if the thread is joinable.
  bool IsJoinable() const noexcept;
  /// Request jthread to stop
  bool RequestStop() noexcept;

  /// Asserts that SIGCHILD as a signal is blocked before thread is started
  /// and after it's started to make sure that it never gets allowed.
  /// This is required by the WAIT system.
  static void AssertSigChildIsBlocked() noexcept;

private:
  SpawnFunction mWork;  // The task to run in the thread
  std::jthread mThread; // The underlying std::thread
  bool mStarted;
};
} // namespace mdb