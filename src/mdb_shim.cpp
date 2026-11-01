#include <arpa/inet.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::array<char, 8> kMagic{ 'M', 'D', 'B', 'S', 'H', 'I', 'M', '\0' };
constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::uint8_t kHelloMessage = 1;
constexpr std::uint8_t kGoMessage = 2;
constexpr std::uint8_t kErrorMessage = 3;
constexpr std::size_t kHeaderSize = 16;
constexpr std::uint32_t kMaxPayloadSize = 1024 * 1024;
constexpr std::uint32_t kMaxArgumentCount = 4096;

struct Options
{
  pid_t mTracerPid{ -1 };
  std::string_view mSocketPath;
  int mProgramIndex{ -1 };
};

void
PrintUsage(const char *program)
{
  std::fprintf(stderr, "Usage: %s --tracer-pid <pid> --socket <path> -- <program> [args...]\n", program);
}

bool
ParsePid(std::string_view text, pid_t &result)
{
  long parsed = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (error != std::errc{} || end != text.data() + text.size() || parsed <= 0 ||
      parsed > std::numeric_limits<pid_t>::max()) {
    return false;
  }
  result = static_cast<pid_t>(parsed);
  return true;
}

bool
ParseOptions(int argc, char **argv, Options &options)
{
  bool gotTracerPid = false;
  bool gotSocketPath = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{ argv[index] };
    if (argument == "--") {
      options.mProgramIndex = index + 1;
      return gotTracerPid && gotSocketPath && options.mProgramIndex < argc &&
             argv[options.mProgramIndex][0] != '\0';
    }
    if (index + 1 >= argc) {
      return false;
    }
    const std::string_view value{ argv[++index] };
    if (argument == "--tracer-pid" && !gotTracerPid) {
      if (!ParsePid(value, options.mTracerPid)) {
        return false;
      }
      gotTracerPid = true;
    } else if (argument == "--socket" && !gotSocketPath) {
      if (value.empty()) {
        return false;
      }
      options.mSocketPath = value;
      gotSocketPath = true;
    } else {
      return false;
    }
  }
  return false;
}

bool
WriteAll(int fd, const void *data, std::size_t size)
{
  const auto *bytes = static_cast<const std::uint8_t *>(data);
  while (size > 0) {
    const ssize_t written = send(fd, bytes, size, MSG_NOSIGNAL);
    if (written == -1 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      return false;
    }
    bytes += written;
    size -= static_cast<std::size_t>(written);
  }
  return true;
}

bool
ReadAll(int fd, void *data, std::size_t size)
{
  auto *bytes = static_cast<std::uint8_t *>(data);
  while (size > 0) {
    const ssize_t read = recv(fd, bytes, size, 0);
    if (read == -1 && errno == EINTR) {
      continue;
    }
    if (read <= 0) {
      return false;
    }
    bytes += read;
    size -= static_cast<std::size_t>(read);
  }
  return true;
}

void
AppendU32(std::vector<std::uint8_t> &buffer, std::uint32_t value)
{
  value = htonl(value);
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(&value);
  buffer.insert(buffer.end(), bytes, bytes + sizeof(value));
}

bool
SendFrame(int fd, std::uint8_t type, std::span<const std::uint8_t> payload)
{
  if (payload.size() > kMaxPayloadSize) {
    return false;
  }
  std::array<std::uint8_t, kHeaderSize> header{};
  std::memcpy(header.data(), kMagic.data(), kMagic.size());
  const std::uint16_t version = htons(kProtocolVersion);
  std::memcpy(header.data() + 8, &version, sizeof(version));
  header[10] = type;
  const std::uint32_t payloadSize = htonl(static_cast<std::uint32_t>(payload.size()));
  std::memcpy(header.data() + 12, &payloadSize, sizeof(payloadSize));
  return WriteAll(fd, header.data(), header.size()) && WriteAll(fd, payload.data(), payload.size());
}

bool
ReceiveResponse(int fd, std::string &errorMessage)
{
  std::array<std::uint8_t, kHeaderSize> header{};
  if (!ReadAll(fd, header.data(), header.size())) {
    errorMessage = "connection closed before the controller replied";
    return false;
  }

  std::uint16_t version = 0;
  std::memcpy(&version, header.data() + 8, sizeof(version));
  version = ntohs(version);
  std::uint32_t payloadSize = 0;
  std::memcpy(&payloadSize, header.data() + 12, sizeof(payloadSize));
  payloadSize = ntohl(payloadSize);
  if (!std::equal(kMagic.begin(), kMagic.end(), header.begin()) || version != kProtocolVersion ||
      header[11] != 0 || payloadSize > kMaxPayloadSize) {
    errorMessage = "controller sent an invalid shim protocol response";
    return false;
  }

  if (header[10] == kGoMessage && payloadSize == 0) {
    return true;
  }
  if (header[10] != kErrorMessage || payloadSize == 0 || payloadSize > 4096) {
    errorMessage = "controller sent an invalid shim protocol response";
    return false;
  }
  errorMessage.resize(payloadSize);
  if (!ReadAll(fd, errorMessage.data(), errorMessage.size())) {
    errorMessage = "connection closed while reading the controller error";
    return false;
  }
  return false;
}

int
Connect(const Options &options)
{
  sockaddr_un address{};
  if (options.mSocketPath.size() >= sizeof(address.sun_path)) {
    std::fprintf(stderr, "mdb_shim: socket path is too long\n");
    return -1;
  }

  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd == -1) {
    std::fprintf(stderr, "mdb_shim: socket() failed: %s\n", std::strerror(errno));
    return -1;
  }
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, options.mSocketPath.data(), options.mSocketPath.size());
  address.sun_path[options.mSocketPath.size()] = '\0';
  if (connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == -1) {
    std::fprintf(stderr,
      "mdb_shim: connect(%s) failed: %s\n",
      std::string(options.mSocketPath).c_str(),
      std::strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

} // namespace

int
main(int argc, char **argv)
{
  Options options{};
  if (!ParseOptions(argc, argv, options)) {
    PrintUsage(argv[0]);
    return 2;
  }

  if (prctl(PR_SET_PTRACER, options.mTracerPid, 0, 0, 0) == -1 && errno != EINVAL) {
    std::fprintf(stderr, "mdb_shim: prctl(PR_SET_PTRACER) failed: %s\n", std::strerror(errno));
    return 1;
  }

  std::vector<std::uint8_t> payload{};
  const int argumentCount = argc - options.mProgramIndex;
  if (argumentCount > static_cast<int>(kMaxArgumentCount)) {
    std::fprintf(stderr, "mdb_shim: too many target arguments\n");
    return 1;
  }
  payload.reserve(8 + static_cast<std::size_t>(argumentCount) * 4);
  AppendU32(payload, static_cast<std::uint32_t>(getpid()));
  AppendU32(payload, static_cast<std::uint32_t>(argumentCount));
  for (int index = options.mProgramIndex; index < argc; ++index) {
    const std::string_view argument{ argv[index] };
    if (argument.size() > kMaxPayloadSize || payload.size() + 4 + argument.size() > kMaxPayloadSize) {
      std::fprintf(stderr, "mdb_shim: target arguments exceed the handshake size limit\n");
      return 1;
    }
    AppendU32(payload, static_cast<std::uint32_t>(argument.size()));
    payload.insert(payload.end(), argument.begin(), argument.end());
  }

  const int socketFd = Connect(options);
  if (socketFd == -1) {
    return 1;
  }
  if (!SendFrame(socketFd, kHelloMessage, payload)) {
    std::fprintf(stderr, "mdb_shim: failed to send attach handshake\n");
    close(socketFd);
    return 1;
  }

  std::string errorMessage{};
  if (!ReceiveResponse(socketFd, errorMessage)) {
    std::fprintf(stderr, "mdb_shim: %s\n", errorMessage.c_str());
    close(socketFd);
    return 1;
  }
  close(socketFd);

  execv(argv[options.mProgramIndex], &argv[options.mProgramIndex]);
  std::fprintf(stderr, "mdb_shim: execv(%s) failed: %s\n", argv[options.mProgramIndex], std::strerror(errno));
  return 127;
}
