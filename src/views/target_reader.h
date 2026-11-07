/** LICENSE TEMPLATE */
#pragma once

#include <interface/tracee_command/supervisor_state.h>
#include <views/evaluator.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace mdb::views {

class TargetReader final : public MemoryReader
{
  tc::SupervisorState *mSupervisor;
  struct Block
  {
    u64 mAddress;
    std::vector<u8> mBytes;
  };
  std::vector<Block> mBlocks;

  bool
  ReadTarget(u64 address, u64 size, std::vector<u8> &out) noexcept
  {
    auto result = mSupervisor->SafeRead(AddrPtr{ address }, size);
    if (!result.is_expected()) {
      return false;
    }
    auto bytes = result.take_value()->Span();
    if (bytes.size() < size) {
      return false;
    }
    out.assign(bytes.begin(), bytes.begin() + static_cast<i64>(size));
    return true;
  }

public:
  explicit TargetReader(tc::SupervisorState *supervisor) noexcept : mSupervisor(supervisor) {}
  bool
  Read(u64 address, std::span<u8> out) noexcept final
  {
    constexpr u64 pageSize = 4096;
    constexpr u64 minimumRead = 64;
    constexpr size_t maxCachedBlocks = 32;
    if (out.empty()) {
      return true;
    }
    for (const auto &block : mBlocks) {
      if (address >= block.mAddress && address - block.mAddress + out.size() <= block.mBytes.size()) {
        std::memcpy(out.data(), block.mBytes.data() + (address - block.mAddress), out.size());
        return true;
      }
    }
    if (!mSupervisor) {
      return false;
    }
    const u64 pageRemain = pageSize - address % pageSize;
    const u64 widened = std::max<u64>(out.size(), std::min(minimumRead, pageRemain));
    Block block{ .mAddress = address };
    if (!ReadTarget(address, widened, block.mBytes) &&
        (widened == out.size() || !ReadTarget(address, out.size(), block.mBytes))) {
      return false;
    }
    std::memcpy(out.data(), block.mBytes.data(), out.size());
    if (mBlocks.size() < maxCachedBlocks) {
      mBlocks.push_back(std::move(block));
    }
    return true;
  }
};

} // namespace mdb::views
