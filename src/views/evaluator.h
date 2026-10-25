/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <views/bound_ir.h>

// std
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace mdb::views {

class MemoryReader
{
public:
  virtual ~MemoryReader() noexcept = default;
  // Read exactly `out.size()` bytes at `address`. A partial read is a failed read.
  virtual bool Read(u64 address, std::span<u8> out) noexcept = 0;
};

struct EvalError
{
  std::string mMessage;
  SourceLoc mLoc;
};

static constexpr u32 kNoRoot = std::numeric_limits<u32>::max();
static constexpr u64 kDefaultBudget = 100'000;

struct EvalValue
{
  TypeId mType{ kNoType };
  bool mIsLValue{ false };
  bool mIsLocalRef{ false };
  u32 mRefStack{ 0 };
  u32 mRefSlot{ 0 };
  // lvalues
  u64 mAddress{ 0 };
  u32 mRoot{ kNoRoot };
  u32 mBitOffset{ 0 };
  u16 mBitSize{ 0 };
  // integers, bools, enums and pointers: the bit pattern, sign extended to 64 bits for signed types.
  // array providers: the base address.
  u64 mBits{ 0 };
  double mFloat{ 0.0 };
  // text
  std::string mText;
  bool mTruncated{ false };
  // array providers: the number of elements
  u64 mCount{ 0 };
};

struct ArrayChildren
{
  u64 mBase{ 0 };
  u64 mCount{ 0 };
  TypeId mElement{ kNoType };
};

struct Frame
{
  const BoundView *mView;
  u32 mFunction;
  u32 mPc;
  u32 mLocalBase;
};

struct ProgramState
{
  std::vector<Frame> mFrames;
  std::vector<EvalValue> mLocals;
  std::vector<EvalValue> mOperands;
  bool mDone{ false };
};

struct RunLimits
{
  u64 mMaxYields{ std::numeric_limits<u64>::max() };
  u64 mBudget{ kDefaultBudget };
  u32 mMaxDepth{ 1024 };
};

struct YieldedElement
{
  TypeId mType;
  std::optional<u64> mAddress;
  EvalValue mValue;
  std::optional<std::string> mKey;
};

struct Root
{
  TypeId mType;
  std::optional<u64> mAddress;
  std::span<const u8> mBytes;
};

enum class RunStop : u8
{
  Done,
  Paused,
  Budget,
};

struct RunResult
{
  RunStop mStop;
  u64 mSteps;
};

/**
 * Evaluates the bytecode of a bound view against the supplied roots. Let values are cached at most once per
 * evaluator and `(view, let, this address)` tuple.
 */
class Evaluator
{
  const BoundView &mView;
  MemoryReader &mReader;
  std::vector<Root> mRoots;
  using LetKey = std::tuple<const BoundView *, u32, u64>;
  std::map<LetKey, EvalValue> mLetValues;
  std::set<LetKey> mLetInProgress;
  u64 mStepsUsed{ 0 };

  using Result = std::expected<EvalValue, EvalError>;

  bool ReadBytes(u64 address, std::span<u8> out) noexcept;
  Result Load(const EvalValue &lvalue, SourceLoc loc) noexcept;
  Result EvalText(
    const Instruction &instruction, const EvalValue &pointer, const std::optional<EvalValue> &length) noexcept;
  Result EvalBinary(Op op, const EvalValue &lhs, const EvalValue &rhs, TypeId resultType, SourceLoc loc) noexcept;
  Result Convert(const EvalValue &value, TypeId to, SourceLoc loc) noexcept;
  [[nodiscard]] EvalValue MakeInteger(TypeId type, u64 bits) const noexcept;
  [[nodiscard]] static EvalError ErrorAt(SourceLoc loc, std::string message) noexcept;

  std::expected<RunResult, EvalError> RunWithBudget(ProgramState &state,
    const RunLimits &limits,
    u64 &stepsLeft,
    const std::function<void(const YieldedElement &)> &onYield,
    bool allowYield) noexcept;
  Result RunToCompletion(ProgramState &state, const RunLimits &limits, u64 &stepsLeft) noexcept;

public:
  Evaluator(const BoundView &view, MemoryReader &reader, std::span<const Root> roots) noexcept;

  ProgramState Start(u32 function, std::span<const EvalValue> args) noexcept;
  std::expected<RunResult, EvalError> Run(ProgramState &state,
    const RunLimits &limits,
    const std::function<void(const YieldedElement &)> &onYield) noexcept;
  Result Call(u32 function, std::span<const EvalValue> args, u64 budget) noexcept;

  std::expected<std::string, EvalError> Summary(u64 budget = kDefaultBudget) noexcept;
  std::expected<ArrayChildren, EvalError> Children(u64 budget = kDefaultBudget) noexcept;
  std::expected<EvalValue, EvalError> ThisArgument() const noexcept;
  std::expected<i64, EvalError> Size(u64 budget = kDefaultBudget) noexcept;
  std::expected<std::string, EvalError> Format(const EvalValue &value, FormatSpec format) noexcept;
};

} // namespace mdb::views
