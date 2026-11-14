/** LICENSE TEMPLATE */
#pragma once

// mdb
#include "common.h"
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

// An object's dynamic (most derived) type, and the address of the complete object.
struct DynamicObject
{
  sym::Type *mType;
  u64 mAddress;
};

// What the evaluator needs from the debugger, beyond memory: views for other types, for view() and for formatting
// records with their summary, and dynamic types.
class RuntimeServices
{
public:
  virtual ~RuntimeServices() noexcept = default;
  // The view for values of `type`, or nullptr
  virtual const CodeObject *ViewFor(sym::Type *type) noexcept = 0;
  // The dynamic type of the object of static type `type` at `address`. nullopt if the type has no vtable, or the
  // dynamic type can't be determined.
  virtual std::optional<DynamicObject> DynamicTypeOf(sym::Type *type, u64 address) noexcept = 0;
};

static constexpr u32 kNoRoot = std::numeric_limits<u32>::max();
// Summaries that format records with their own summary can nest this deep.
static constexpr u32 kMaxNestedSummaries = 8;
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
  u8 mFlags{ 0 };     // kTruncated for Text and ArrayProvider, kVerbatim for Text.
  // Scalar bits (floats are a bit-cast double), LValue address, LocalRef slot, Text handle, or ArrayProvider base.
  u64 mA{ 0 };
  // LValue root, LocalRef stack, or ArrayProvider count; otherwise unused.
  u64 mB{ 0 };

  static constexpr u8 kTruncated = 1;
  // Text that is formatted as it is, without quotes: a summary
  static constexpr u8 kVerbatim = 2;

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
    MDB_ENSURE(lvalue.IsLValue());
    MDB_ENSURE(bitOffset < 8);
    MDB_ENSURE(bitSize > 0 && bitSize <= 64);
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
  Text(TypeId type, u32 handle, bool truncated, bool verbatim = false) noexcept
  {
    EvalValue value;
    value.mType = type;
    value.mKind = ValueKind::Text;
    value.mA = handle;
    value.mFlags = static_cast<u8>((truncated ? kTruncated : 0) | (verbatim ? kVerbatim : 0));
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
  // The same value, described by type `type` of another code object (see Op::CallView).
  [[nodiscard]] EvalValue
  Retyped(TypeId type) const noexcept
  {
    EvalValue value = *this;
    value.mType = type;
    return value;
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
    MDB_ENSURE(mKind == ValueKind::Scalar || mKind == ValueKind::ArrayProvider);
    return mA;
  }
  [[nodiscard]] double
  AsFloat() const noexcept
  {
    MDB_ENSURE(mKind == ValueKind::Scalar);
    return std::bit_cast<double>(mA);
  }
  [[nodiscard]] u64
  Address() const noexcept
  {
    MDB_ENSURE(mKind == ValueKind::LValue);
    return mA;
  }
  [[nodiscard]] u32
  Root() const noexcept
  {
    MDB_ENSURE(mKind == ValueKind::LValue);
    return static_cast<u32>(mB);
  }
  [[nodiscard]] u8
  BitOffset() const noexcept
  {
    MDB_ENSURE(mKind == ValueKind::LValue);
    return mBitOffset;
  }
  [[nodiscard]] u8
  BitSize() const noexcept
  {
    MDB_ENSURE(mKind == ValueKind::LValue);
    return mBitSize;
  }
  [[nodiscard]] u32
  RefSlot() const noexcept
  {
    MDB_ENSURE(mKind == ValueKind::LocalRef);
    return static_cast<u32>(mA);
  }
  [[nodiscard]] u32
  RefStack() const noexcept
  {
    MDB_ENSURE(mKind == ValueKind::LocalRef);
    return static_cast<u32>(mB);
  }
  [[nodiscard]] u64
  Count() const noexcept
  {
    MDB_ENSURE(mKind == ValueKind::ArrayProvider);
    return mB;
  }
  [[nodiscard]] bool
  Truncated() const noexcept
  {
    MDB_ENSURE(mKind == ValueKind::Text || mKind == ValueKind::ArrayProvider);
    return (mFlags & kTruncated) != 0;
  }
  [[nodiscard]] bool
  Verbatim() const noexcept
  {
    MDB_ENSURE(mKind == ValueKind::Text);
    return (mFlags & kVerbatim) != 0;
  }
  [[nodiscard]] u32
  TextHandle() const noexcept
  {
    MDB_ENSURE(mKind == ValueKind::Text);
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
  const CodeObject *mCodeObject;
  u32 mFunction;
  u32 mPc;
  u32 mLocalBase;
  // For a call into another view: the caller's type for the returned value. kNoType: the type is the callee's.
  TypeId mReturnAs{ kNoType };
};

struct ProgramState
{
  std::vector<Frame> mFrames;
  std::vector<EvalValue> mLocals;
  std::vector<EvalValue> mOperands;
  bool mDone{ false };

  EvalValue PopOperand() noexcept;
};

struct RunLimits
{
  u64 mMaxYields{ std::numeric_limits<u64>::max() };
  u64 mBudget{ kDefaultBudget };
  u32 mMaxDepth{ 1024 };
  u32 mStepsLeft;
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
  std::optional<std::string> mUnavailable;
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
 * Evaluates the bytecode of a code object against the supplied roots. Let values are cached at most once per
 * evaluator and `(code object, let, this address)` tuple.
 */
class Evaluator
{
  using LetKey = std::tuple<const CodeObject *, u32, u64>;

  const CodeObject &mCodeObject;
  MemoryReader &mReader;
  RuntimeServices *mServices{ nullptr };
  std::vector<Root> mRoots;
  // Own decoded text without moving strings as more text is added.
  std::deque<std::string> mOwnedTexts;
  // Handles index views into mOwnedTexts or immutable program constants.
  std::vector<std::string_view> mTexts;
  std::map<LetKey, EvalValue> mLetValues;
  std::set<LetKey> mLetInProgress;
  u64 mStepsUsed{ 0 };
  u32 mSummaryDepth{ 0 };
  // Dynamic dispatch of view calls: (callee, dynamic type) -> the code object and function to run, or null to run
  // the static callee.
  struct DispatchTarget
  {
    const CodeObject *mCode;
    u32 mFunction;
  };
  std::map<std::pair<const ViewCallee *, sym::Type *>, std::optional<DispatchTarget>> mDispatch;

  using Result = std::expected<EvalValue, EvalError>;

  bool ReadBytes(u64 address, std::span<u8> out) noexcept;
  Result Load(const CodeObject &code, const EvalValue &lvalue, SourceLoc loc) noexcept;
  Result EvalText(
    const Instruction &instruction, const EvalValue &pointer, const std::optional<EvalValue> &length) noexcept;
  Result EvalBinary(const CodeObject &code,
    Op op,
    const EvalValue &lhs,
    const EvalValue &rhs,
    TypeId resultType,
    SourceLoc loc) noexcept;
  Result Convert(const CodeObject &code, const EvalValue &value, TypeId to, SourceLoc loc) noexcept;
  [[nodiscard]] EvalValue MakeText(TypeId type, std::string text, bool truncated, bool verbatim = false) noexcept;
  [[nodiscard]] EvalValue MakeTextConstant(TypeId type, std::string_view constant) noexcept;
  [[nodiscard]] static EvalValue MakeInteger(const CodeObject &code, TypeId type, u64 bits) noexcept;
  [[nodiscard]] static EvalError ErrorAt(SourceLoc loc, std::string message) noexcept;

  ProgramState Start(const CodeObject &code, u32 function, std::span<const EvalValue> args) noexcept;
  std::expected<RunResult, EvalError> RunWithBudget(ProgramState &state,
    const RunLimits &limits,
    u64 &stepsLeft,
    const std::function<void(const YieldedElement &)> &onYield,
    bool allowYield) noexcept;
  Result RunToCompletion(ProgramState &state, const RunLimits &limits, u64 &stepsLeft) noexcept;

  // Where a view call on the object at `address` goes: the view of its dynamic type, if that view has a function
  // with the same name and signature, otherwise the static callee. `address` becomes the callee's `this`.
  std::expected<DispatchTarget, std::string> Dispatch(const ViewCallee &call, u64 &address) noexcept;
  // The summary of `code` for the object at `thisAddress`, which is a pointer to the type the view matched.
  std::expected<std::string, EvalError> SummaryOf(
    const CodeObject &code, u64 thisAddress, const RunLimits &limits, u64 &stepsLeft) noexcept;
  std::expected<std::string, EvalError> FormatIn(
    const CodeObject &code, const EvalValue &value, FormatSpec format) noexcept;
  // A record at `address` formatted with its view's summary, if it has one.
  std::optional<std::expected<std::string, EvalError>> FormatRecord(sym::Type *type, u64 address) noexcept;

public:
  Evaluator(const CodeObject &code, MemoryReader &reader, std::span<const Root> roots) noexcept;

  ProgramState Start(u32 function, std::span<const EvalValue> args) noexcept;
  std::expected<RunResult, EvalError> Run(ProgramState &state,
    const RunLimits &limits,
    const std::function<void(const YieldedElement &)> &onYield) noexcept;
  Result Call(u32 function, std::span<const EvalValue> args, u64 budget) noexcept;

  std::expected<std::string, EvalError> Summary(u64 budget = kDefaultBudget) noexcept;
  std::string FormatTemplate(u64 budget = kDefaultBudget) noexcept;
  // Without services, view calls run their static callee and records are formatted as their type name.
  void
  SetServices(RuntimeServices *services) noexcept
  {
    mServices = services;
  }
  std::expected<ArrayChildren, EvalError> Children(u64 budget = kDefaultBudget) noexcept;
  std::expected<EvalValue, EvalError> ThisArgument() const noexcept;
  std::expected<std::string, EvalError> Format(const EvalValue &value, FormatSpec format) noexcept;
  // Text handles are evaluator-local. This is safe because text never enters locals, yields, or checkpoints.
  [[nodiscard]] std::string_view TextOf(const EvalValue &value) const noexcept;
};

} // namespace mdb::views
