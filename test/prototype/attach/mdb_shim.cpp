#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/prctl.h>
#include <unistd.h>

namespace {

void
PrintUsage(const char *program)
{
  std::fprintf(stderr, "Usage: %s --tracer-pid <pid> -- <program> [args...]\n", program);
}

} // namespace

int
main(int argc, char **argv)
{
  if (argc < 5 || std::strcmp(argv[1], "--tracer-pid") != 0 || std::strcmp(argv[3], "--") != 0) {
    PrintUsage(argv[0]);
    return 2;
  }

  char *end = nullptr;
  errno = 0;
  const long tracerPid = std::strtol(argv[2], &end, 10);
  if (errno != 0 || end == argv[2] || *end != '\0' || tracerPid <= 0) {
    PrintUsage(argv[0]);
    return 2;
  }

  if (prctl(PR_SET_PTRACER, tracerPid, 0, 0, 0) == -1 && errno != EINVAL) {
    std::fprintf(stderr, "prctl(PR_SET_PTRACER) failed: %s\n", std::strerror(errno));
    return 1;
  }

  std::fprintf(stderr, "ready %ld\n", static_cast<long>(getpid()));
  std::fflush(stderr);

  std::string line;
  if (!std::getline(std::cin, line)) {
    return 1;
  }

  execv(argv[4], &argv[4]);
  std::fprintf(stderr, "execv(%s) failed: %s\n", argv[4], std::strerror(errno));
  return 127;
}
