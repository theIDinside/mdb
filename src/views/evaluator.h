/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <views/bound_ir.h>

// std
#include <bit>
#include <cassert>
#include <deque>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
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

enum class ValueKind : u8
{
  Scalar,
  LValue,
  LocalRef,
  Text,
  ArrayProvider,
};

// mA and mB depend on Kind: Scalar = bits / unused; LValue = address / root; LocalRef = slot / stack;
// Text = evaluator-local handle / unused; ArrayProvider = base / count. Float scalars store a bit-cast double.
class EvalValue
{
private:
  TypeId mType{ kNoType };
  ValueKind mKind{ ValueKind::Scalar };
  u8 mBitOffset{ 0 }; // LValue bit offset inside the byte at mA (0..7).
  u8 mBitSize{ 0 };   // LValue bitfield width; 0 means not a bitfield.
  u8 mFlags{ 0 };     // kTruncated for Text and ArrayProvider.
  // Scalar bits (floats are a bit-cast double), LValue address, LocalRef slot, Text handle, or ArrayProvider base.
  u64 mA{ 0 };
  // LValue root, LocalRef stack, or ArrayProvider count; otherwise unused.
  u64 mB{ 0 };

  static constexpr u8 kTruncated = 1;

public:
  EvalValue() noexcept = default;

  [[nodiscard]] static EvalValue
  Scalar(TypeId type, u64 bits) noexcept
  {
    EvalValue value;
    value.mType = type;
    value.mKind = ValueKind::Scalar;
    value.mA = bits;
    return value;
  }

  [[nodiscard]] static EvalValue
  Float(TypeId type, double number) noexcept
  {
    EvalValue value;
    value.mType = type;
    value.mKind = ValueKind::Scalar;
    value.mA = std::bit_cast<u64>(number);
    return value;
  }

  [[nodiscard]] static EvalValue
  LValue(TypeId type, u64 address) noexcept
  {
    EvalValue value;
    value.mType = type;
    value.mKind = ValueKind::LValue;
    value.mA = address;
    value.mB = kNoRoot;
    return value;
  }

  [[nodiscard]] static EvalValue
  RootLValue(TypeId type, u32 root, u64 offset) noexcept
  {
    EvalValue value = LValue(type, offset);
    value.mB = root;
    return value;
  }

  [[nodiscard]] static EvalValue
  Bitfield(EvalValue lvalue, u32 bitOffset, u16 bitSize) noexcept
  {
    assert(lvalue.IsLValue());
    assert(bitOffset < 8);
    assert(bitSize > 0 && bitSize <= 64);
    lvalue.mBitOffset = static_cast<u8>(bitOffset);
    lvalue.mBitSize = static_cast<u8>(bitSize);
    return lvalue;
  }

  [[nodiscard]] static EvalValue
  LocalRef(TypeId type, u32 slot) noexcept
  {
    EvalValue value;
    value.mType = type;
    value.mKind = ValueKind::LocalRef;
    value.mA = slot;
    value.mB = 0;
    return value;
  }

  [[nodiscard]] static EvalValue
  Text(TypeId type, u32 handle, bool truncated) noexcept
  {
    EvalValue value;
    value.mType = type;
    value.mKind = ValueKind::Text;
    value.mA = handle;
    value.mFlags = truncated ? kTruncated : 0;
    return value;
  }

  [[nodiscard]] static EvalValue
  ArrayProvider(TypeId type, u64 base, u64 count, bool truncated) noexcept
  {
    EvalValue value;
    value.mType = type;
    value.mKind = ValueKind::ArrayProvider;
    value.mA = base;
    value.mB = count;
    value.mFlags = truncated ? kTruncated : 0;
    return value;
  }

  [[nodiscard]] TypeId
  Type() const noexcept
  {
    return mType;
  }
  [[nodiscard]] ValueKind
  Kind() const noexcept
  {
    return mKind;
  }
  [[nodiscard]] bool
  IsLValue() const noexcept
  {
    return mKind == ValueKind::LValue;
  }
  [[nodiscard]] bool
  IsLocalRef() const noexcept
  {
    return mKind == ValueKind::LocalRef;
  }
  [[nodiscard]] u64
  Bits() const noexcept
  {
    assert(mKind == ValueKind::Scalar || mKind == ValueKind::ArrayProvider);
    return mA;
  }
  [[nodiscard]] double
  AsFloat() const noexcept
  {
    assert(mKind == ValueKind::Scalar);
    return std::bit_cast<double>(mA);
  }
  [[nodiscard]] u64
  Address() const noexcept
  {
    assert(mKind == ValueKind::LValue);
    return mA;
  }
  [[nodiscard]] u32
  Root() const noexcept
  {
    assert(mKind == ValueKind::LValue);
    return static_cast<u32>(mB);
  }
  [[nodiscard]] u8
  BitOffset() const noexcept
  {
    assert(mKind == ValueKind::LValue);
    return mBitOffset;
  }
  [[nodiscard]] u8
  BitSize() const noexcept
  {
    assert(mKind == ValueKind::LValue);
    return mBitSize;
  }
  [[nodiscard]] u32
  RefSlot() const noexcept
  {
    assert(mKind == ValueKind::LocalRef);
    return static_cast<u32>(mA);
  }
  [[nodiscard]] u32
  RefStack() const noexcept
  {
    assert(mKind == ValueKind::LocalRef);
    return static_cast<u32>(mB);
  }
  [[nodiscard]] u64
  Count() const noexcept
  {
    assert(mKind == ValueKind::ArrayProvider);
    return mB;
  }
  [[nodiscard]] bool
  Truncated() const noexcept
  {
    assert(mKind == ValueKind::Text || mKind == ValueKind::ArrayProvider);
    return (mFlags & kTruncated) != 0;
  }
  [[nodiscard]] u32
  TextHandle() const noexcept
  {
    assert(mKind == ValueKind::Text);
    return static_cast<u32>(mA);
  }
};
static_assert(std::is_trivially_copyable_v<EvalValue>);
static_assert(sizeof(EvalValue) == 24);

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
  // Own decoded text without moving strings as more text is added.
  std::deque<std::string> mOwnedTexts;
  // Handles index views into mOwnedTexts or immutable program constants.
  std::vector<std::string_view> mTexts;
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
  [[nodiscard]] EvalValue MakeText(TypeId type, std::string text, bool truncated) noexcept;
  [[nodiscard]] EvalValue MakeTextConstant(TypeId type, std::string_view constant) noexcept;
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
  std::expected<std::string, EvalError> Format(const EvalValue &value, FormatSpec format) noexcept;
  // Text handles are evaluator-local. This is safe because text never enters locals, yields, or checkpoints.
  [[nodiscard]] std::string_view TextOf(const EvalValue &value) const noexcept;
};

} // namespace mdb::views
