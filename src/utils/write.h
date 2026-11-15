#pragma once

#include <array>
#include <cerrno>
#include <concepts>
#include <limits>
#include <string>
#include <string_view>
#include <sys/uio.h>

template <typename T>
  requires requires(T &t) {
    { t.ToParam() } -> std::convertible_to<iovec>;
  }

constexpr iovec
ToIoVec(T &value) noexcept
{
  return value.ToParam();
}

constexpr iovec
ToIoVec(std::string_view value) noexcept
{
  // writev only reads the buffer, despite iovec's mutable pointer type.
  return { .iov_base = const_cast<char *>(value.data()), .iov_len = value.size() };
}

constexpr iovec
ToIoVec(const std::string &value) noexcept
{
  return ToIoVec(std::string_view{ value });
}

// Custom types can also define ToIoVec(T&) in their own namespace (found by ADL).
template <typename T>
concept SupportsIoVec = requires(T &t) {
  { ToIoVec(t) } -> std::convertible_to<iovec>;
};

template <SupportsIoVec... Args>
[[nodiscard]] constexpr bool
WriteVectored(int fileDescriptor, Args &&...args)
{
  static_assert(sizeof...(Args) <= std::numeric_limits<int>::max());
  std::array<iovec, sizeof...(Args)> writes{ ToIoVec(args)... };
  std::size_t first = 0;
  while (first < writes.size()) {
    if (writes[first].iov_len == 0) {
      ++first;
      continue;
    }

    const auto result = ::writev(fileDescriptor, writes.data() + first, static_cast<int>(writes.size() - first));
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (result == 0) {
      errno = EIO;
      return false;
    }

    auto remaining = static_cast<std::size_t>(result);
    while (first < writes.size() && remaining >= writes[first].iov_len) {
      remaining -= writes[first].iov_len;
      ++first;
    }
    if (remaining != 0) {
      writes[first].iov_base = static_cast<char *>(writes[first].iov_base) + remaining;
      writes[first].iov_len -= remaining;
    }
  }
  return true;
}
