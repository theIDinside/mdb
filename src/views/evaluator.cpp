/** LICENSE TEMPLATE */
#include "evaluator.h"

// mdb
#include <views/text.h>

// std
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <format>

namespace mdb::views {

static constexpr u64 kPageSize = 4096;
static constexpr u64 kStringChunk = 256;

static u64
SignExtend(u64 bits, u32 sizeInBits) noexcept
{
  if (sizeInBits == 0 || sizeInBits >= 64) {
    return bits;
  }
  const u64 signBit = 1ULL << (sizeInBits - 1);
  const u64 mask = (1ULL << sizeInBits) - 1;
  bits &= mask;
  return (bits ^ signBit) - signBit;
}

static u64
Truncate(u64 bits, u32 sizeInBits) noexcept
{
  if (sizeInBits == 0 || sizeInBits >= 64) {
    return bits;
  }
  return bits & ((1ULL << sizeInBits) - 1);
}

Evaluator::Evaluator(const BoundView &view, MemoryReader &reader, std::span<const Root> roots) noexcept
    : mView(view), mReader(reader), mRoots(roots.begin(), roots.end())
{
}

EvalError
Evaluator::ErrorAt(SourceLoc loc, std::string message) noexcept
{
  return EvalError{ .mMessage = std::move(message), .mLoc = loc };
}

EvalValue
Evaluator::MakeText(TypeId type, std::string text, bool truncated) noexcept
{
  mOwnedTexts.push_back(std::move(text));
  const u32 handle = static_cast<u32>(mTexts.size());
  mTexts.push_back(mOwnedTexts.back());
  return EvalValue::Text(type, handle, truncated);
}

EvalValue
Evaluator::MakeTextConstant(TypeId type, std::string_view constant) noexcept
{
  const u32 handle = static_cast<u32>(mTexts.size());
  mTexts.push_back(constant);
  return EvalValue::Text(type, handle, false);
}

EvalValue
Evaluator::MakeInteger(TypeId type, u64 bits) const noexcept
{
  const BoundType &t = mView.Type(type);
  u64 normalized = t.mIsSigned ? SignExtend(bits, t.mSize * 8) : Truncate(bits, t.mSize * 8);
  if (t.mKind == TypeKind::Bool) {
    normalized = bits != 0 ? 1 : 0;
  }
  return EvalValue::Scalar(type, normalized);
}

bool
Evaluator::ReadBytes(u64 address, std::span<u8> out) noexcept
{
  if (out.empty()) {
    return true;
  }
  for (const Root &root : mRoots) {
    if (!root.mAddress || address < *root.mAddress) {
      continue;
    }
    const u64 offset = address - *root.mAddress;
    if (offset <= root.mBytes.size() && out.size() <= root.mBytes.size() - offset) {
      std::memcpy(out.data(), root.mBytes.data() + offset, out.size());
      return true;
    }
  }
  return mReader.Read(address, out);
}

Evaluator::Result
Evaluator::Load(const EvalValue &lvalue, SourceLoc loc) noexcept
{
  const BoundType &type = mView.Type(lvalue.Type());
  if (!type.IsScalar()) {
    return std::unexpected(ErrorAt(loc, std::format("can not read a value of type '{}'", type.mName)));
  }

  const auto read = [&](std::span<u8> out, bool bitfield) {
    if (lvalue.Root() == kNoRoot) {
      return ReadBytes(lvalue.Address(), out);
    }
    if (lvalue.Root() >= mRoots.size()) {
      return false;
    }
    const auto bytes = mRoots[lvalue.Root()].mBytes;
    if (lvalue.Address() <= bytes.size() && out.size() <= bytes.size() - lvalue.Address()) {
      std::memcpy(out.data(), bytes.data() + lvalue.Address(), out.size());
      return true;
    }
    (void)bitfield;
    return false;
  };

  if (lvalue.BitSize() > 0) {
    const u32 byteCount = (lvalue.BitOffset() + lvalue.BitSize() + 7) / 8;
    std::array<u8, 9> raw{};
    if (!read(std::span{ raw }.first(byteCount), true)) {
      const std::string message = lvalue.Root() == kNoRoot
                                    ? std::format("can not read memory at 0x{:x}", lvalue.Address())
                                    : std::format("can not read memory at offset {}", lvalue.Address());
      return std::unexpected(ErrorAt(loc, message));
    }
    u64 low = 0;
    constexpr u32 kLowByteCount = 8;
    std::memcpy(&low, raw.data(), std::min(byteCount, kLowByteCount));
    u64 bits = low >> lvalue.BitOffset();
    if (byteCount > kLowByteCount) {
      bits |= static_cast<u64>(raw[kLowByteCount]) << (64 - lvalue.BitOffset());
    }
    bits = Truncate(bits, lvalue.BitSize());
    const u64 normalized = type.mIsSigned ? SignExtend(bits, lvalue.BitSize()) : bits;
    return EvalValue::Scalar(lvalue.Type(), normalized);
  }

  if (type.mSize == 0 || type.mSize > 8) {
    return std::unexpected(ErrorAt(loc, std::format("unsupported size {} of type '{}'", type.mSize, type.mName)));
  }

  u64 raw = 0;
  if (!read(std::span{ reinterpret_cast<u8 *>(&raw), type.mSize }, false)) {
    const std::string message =
      lvalue.Root() == kNoRoot ? std::format("can not read {} bytes at 0x{:x}", type.mSize, lvalue.Address())
                               : std::format("can not read {} bytes at offset {}", type.mSize, lvalue.Address());
    return std::unexpected(ErrorAt(loc, message));
  }

  if (type.mKind == TypeKind::Float) {
    double number = 0.0;
    if (type.mSize == 4) {
      float f;
      std::memcpy(&f, &raw, 4);
      number = f;
    } else if (type.mSize == 8) {
      std::memcpy(&number, &raw, 8);
    } else {
      return std::unexpected(ErrorAt(loc, std::format("unsupported floating point size {}", type.mSize)));
    }
    return EvalValue::Float(lvalue.Type(), number);
  }
  return MakeInteger(lvalue.Type(), raw);
}

Evaluator::Result
Evaluator::Convert(const EvalValue &value, TypeId to, SourceLoc loc) noexcept
{
  const BoundType &from = mView.Type(value.Type());
  const BoundType &target = mView.Type(to);
  if (target.mKind == TypeKind::Float) {
    double number = 0.0;
    if (from.mKind == TypeKind::Float) {
      number = value.AsFloat();
    } else if (from.mIsSigned) {
      number = static_cast<double>(static_cast<i64>(value.Bits()));
    } else {
      number = static_cast<double>(value.Bits());
    }
    if (target.mSize == 4) {
      number = static_cast<float>(number);
    }
    return EvalValue::Float(to, number);
  }

  if (target.IsInteger() || target.mKind == TypeKind::Pointer) {
    if (from.mKind == TypeKind::Float) {
      if (target.mKind == TypeKind::Bool) {
        return MakeInteger(to, value.AsFloat() != 0.0 ? 1 : 0);
      }
      const u64 bits =
        target.mIsSigned ? static_cast<u64>(static_cast<i64>(value.AsFloat())) : static_cast<u64>(value.AsFloat());
      return MakeInteger(to, bits);
    }
    if (target.mKind == TypeKind::Pointer) {
      return EvalValue::Scalar(to, value.Bits());
    }
    return MakeInteger(to, value.Bits());
  }

  return std::unexpected(
    ErrorAt(loc, std::format("can not convert a value of type '{}' to '{}'", from.mName, target.mName)));
}

Evaluator::Result
Evaluator::EvalBinary(Op op, const EvalValue &lhs, const EvalValue &rhs, TypeId resultType, SourceLoc loc) noexcept
{
  const BoundType &operandType = mView.Type(lhs.Type());
  const bool isFloat = operandType.mKind == TypeKind::Float;
  const bool isSigned = operandType.mIsSigned;
  const u64 a = lhs.Bits();
  const u64 b = rhs.Bits();
  const double fa = lhs.AsFloat();
  const double fb = rhs.AsFloat();

  const auto boolean = [&](bool value) { return MakeInteger(resultType, value ? 1 : 0); };
  const auto arithmetic = [&](u64 bits, double f) {
    if (isFloat) {
      return EvalValue::Float(resultType, f);
    }
    return MakeInteger(resultType, bits);
  };

  switch (op) {
  case Op::Add:
    return arithmetic(a + b, fa + fb);
  case Op::Sub:
    return arithmetic(a - b, fa - fb);
  case Op::Mul:
    return arithmetic(a * b, fa * fb);
  case Op::Div:
  case Op::Mod: {
    if (isFloat) {
      return arithmetic(0, fa / fb);
    }
    if (b == 0) {
      return std::unexpected(ErrorAt(loc, "division by zero"));
    }
    if (isSigned) {
      const i64 sa = static_cast<i64>(a);
      const i64 sb = static_cast<i64>(b);
      if (sb == -1) {
        return arithmetic(op == Op::Div ? 0 - a : 0, 0);
      }
      return arithmetic(static_cast<u64>(op == Op::Div ? sa / sb : sa % sb), 0);
    }
    return arithmetic(op == Op::Div ? a / b : a % b, 0);
  }
  case Op::Shl:
    return arithmetic(a << (b & 63), 0);
  case Op::Shr:
    return arithmetic(isSigned ? static_cast<u64>(static_cast<i64>(a) >> (b & 63)) : a >> (b & 63), 0);
  case Op::BitAnd:
    return arithmetic(a & b, 0);
  case Op::BitOr:
    return arithmetic(a | b, 0);
  case Op::BitXor:
    return arithmetic(a ^ b, 0);
  case Op::Equal:
    return boolean(isFloat ? fa == fb : a == b);
  case Op::NotEqual:
    return boolean(isFloat ? fa != fb : a != b);
  case Op::Less:
    return boolean(isFloat ? fa < fb : (isSigned ? static_cast<i64>(a) < static_cast<i64>(b) : a < b));
  case Op::LessEqual:
    return boolean(isFloat ? fa <= fb : (isSigned ? static_cast<i64>(a) <= static_cast<i64>(b) : a <= b));
  case Op::Greater:
    return boolean(isFloat ? fa > fb : (isSigned ? static_cast<i64>(a) > static_cast<i64>(b) : a > b));
  case Op::GreaterEqual:
    return boolean(isFloat ? fa >= fb : (isSigned ? static_cast<i64>(a) >= static_cast<i64>(b) : a >= b));
  default:
    return std::unexpected(ErrorAt(loc, "internal error: not a binary operator"));
  }
}

Evaluator::Result
Evaluator::EvalText(
  const Instruction &instruction, const EvalValue &pointer, const std::optional<EvalValue> &length) noexcept
{
  const u64 address = pointer.Bits();
  const u64 limit = instruction.mImm;
  const u8 width = instruction.mWidth;
  std::string text;
  bool truncated = false;

  if (length) {
    const i64 unitsRequested = static_cast<i64>(length->Bits());
    if (unitsRequested < 0) {
      return std::unexpected(ErrorAt(instruction.mLoc, std::format("negative text length {}", unitsRequested)));
    }
    const u64 units = std::min<u64>(static_cast<u64>(unitsRequested), limit);
    truncated = units < static_cast<u64>(unitsRequested);
    if (units == 0) {
      return MakeText(instruction.mType, {}, truncated);
    }
    if (address == 0) {
      return std::unexpected(ErrorAt(instruction.mLoc, "text pointer is null"));
    }
    std::vector<u8> bytes(units * width);
    if (!ReadBytes(address, bytes)) {
      return std::unexpected(
        ErrorAt(instruction.mLoc, std::format("can not read {} bytes at 0x{:x}", bytes.size(), address)));
    }
    DecodeText(bytes, width, false, text);
    return MakeText(instruction.mType, std::move(text), truncated);
  }

  if (address == 0) {
    return std::unexpected(ErrorAt(instruction.mLoc, "text pointer is null"));
  }
  std::vector<u8> bytes;
  u64 decodedUnits = 0;
  u64 cursor = address;
  std::vector<u8> chunk;
  while (decodedUnits < limit) {
    const u64 untilPageEnd = kPageSize - (cursor % kPageSize);
    u64 chunkBytes = std::min<u64>({ kStringChunk, untilPageEnd, (limit - decodedUnits) * width });
    chunkBytes -= chunkBytes % width;
    if (chunkBytes == 0) {
      chunkBytes = width;
    }
    chunk.resize(chunkBytes);
    if (!ReadBytes(cursor, chunk)) {
      if (bytes.empty()) {
        return std::unexpected(ErrorAt(instruction.mLoc, std::format("can not read memory at 0x{:x}", cursor)));
      }
      truncated = true;
      break;
    }
    bool terminated = false;
    for (u64 i = 0; i + width <= chunk.size(); i += width) {
      bool isZero = true;
      for (u8 b : std::span{ chunk }.subspan(i, width)) {
        isZero = isZero && b == 0;
      }
      if (isZero) {
        terminated = true;
        break;
      }
      bytes.insert(bytes.end(), chunk.begin() + static_cast<i64>(i), chunk.begin() + static_cast<i64>(i + width));
      if (++decodedUnits >= limit) {
        break;
      }
    }
    if (terminated) {
      break;
    }
    cursor += chunk.size();
    if (decodedUnits >= limit) {
      truncated = true;
    }
  }
  DecodeText(bytes, width, false, text);
  return MakeText(instruction.mType, std::move(text), truncated);
}

ProgramState
Evaluator::Start(u32 function, std::span<const EvalValue> args) noexcept
{
  ProgramState state;
  const Function &fn = mView.mProgram.mFunctions[function];
  state.mFrames.push_back(Frame{ .mView = &mView, .mFunction = function, .mPc = fn.mEntry, .mLocalBase = 0 });
  state.mLocals.assign(args.begin(), args.end());
  for (size_t i = state.mLocals.size(); i < fn.mLocalTypes.size(); ++i) {
    state.mLocals.push_back(EvalValue::Scalar(fn.mLocalTypes[i], 0));
  }
  return state;
}

Evaluator::Result
Evaluator::RunToCompletion(ProgramState &state, const RunLimits &limits, u64 &stepsLeft) noexcept
{
  RunLimits completionLimits = limits;
  completionLimits.mBudget = stepsLeft;
  const auto noYield = [](const YieldedElement &) {};
  auto result = RunWithBudget(state, completionLimits, stepsLeft, noYield, false);
  if (!result) {
    return std::unexpected(std::move(result.error()));
  }
  if (result->mStop == RunStop::Budget) {
    return std::unexpected(ErrorAt({}, std::format("stopped after {} steps", mStepsUsed)));
  }
  if (state.mOperands.empty()) {
    return std::unexpected(ErrorAt({}, "internal error: function returned no value"));
  }
  return state.mOperands.back();
}

std::expected<RunResult, EvalError>
Evaluator::RunWithBudget(ProgramState &state,
  const RunLimits &limits,
  u64 &stepsLeft,
  const std::function<void(const YieldedElement &)> &onYield,
  bool allowYield) noexcept
{
  const u64 stepsAtStart = mStepsUsed;
  u64 yields = 0;
  while (!state.mFrames.empty()) {
    if (yields >= limits.mMaxYields) {
      return RunResult{ .mStop = RunStop::Paused, .mSteps = mStepsUsed - stepsAtStart };
    }
    if (stepsLeft == 0) {
      return RunResult{ .mStop = RunStop::Budget, .mSteps = mStepsUsed - stepsAtStart };
    }
    Frame &frame = state.mFrames.back();
    const Instruction instruction = frame.mView->mProgram.mCode[frame.mPc++];
    --stepsLeft;
    ++mStepsUsed;
    const auto pop = [&]() {
      EvalValue value = std::move(state.mOperands.back());
      state.mOperands.pop_back();
      return value;
    };
    const auto fail = [&](std::string message) -> std::expected<RunResult, EvalError> {
      return std::unexpected(ErrorAt(instruction.mLoc, std::move(message)));
    };

    switch (instruction.mOp) {
    case Op::PushInt:
      state.mOperands.push_back(MakeInteger(instruction.mType, instruction.mImm));
      break;
    case Op::PushFloat:
      state.mOperands.push_back(EvalValue::Float(instruction.mType, instruction.mFloat));
      break;
    case Op::PushText:
      state.mOperands.push_back(
        MakeTextConstant(instruction.mType, frame.mView->mProgram.mConstants[instruction.mA]));
      break;
    case Op::PushRoot: {
      if (instruction.mA >= mRoots.size()) {
        return fail("internal error: root index out of range");
      }
      const Root &root = mRoots[instruction.mA];
      if (root.mAddress) {
        state.mOperands.push_back(EvalValue::LValue(root.mType, *root.mAddress));
      } else {
        state.mOperands.push_back(EvalValue::RootLValue(root.mType, instruction.mA, 0));
      }
      break;
    }
    case Op::LoadLet: {
      const u64 thisAddress = state.mLocals[frame.mLocalBase].Bits();
      const LetKey key{ frame.mView, instruction.mA, thisAddress };
      if (const auto found = mLetValues.find(key); found != mLetValues.end()) {
        state.mOperands.push_back(found->second);
        break;
      }
      if (mLetInProgress.contains(key)) {
        return fail("let binding refers to itself");
      }
      mLetInProgress.insert(key);
      const EvalValue thisPointer = state.mLocals[frame.mLocalBase];
      ProgramState letState = Start(frame.mView->mLetFunctions[instruction.mA], std::span{ &thisPointer, 1 });
      auto value = RunToCompletion(letState, limits, stepsLeft);
      mLetInProgress.erase(key);
      if (!value) {
        return std::unexpected(std::move(value.error()));
      }
      mLetValues.emplace(key, *value);
      state.mOperands.push_back(std::move(*value));
      break;
    }
    case Op::LoadLocal:
      state.mOperands.push_back(state.mLocals[frame.mLocalBase + instruction.mA]);
      break;
    case Op::StoreLocal: {
      EvalValue value = pop();
      if (value.IsLocalRef()) {
        return fail("internal error: storing a reference");
      }
      state.mLocals[frame.mLocalBase + instruction.mA] = std::move(value);
      break;
    }
    case Op::PushRef: {
      const u32 slot = frame.mLocalBase + instruction.mA;
      if (slot >= state.mLocals.size()) {
        return fail("internal error: local reference slot out of range");
      }
      state.mOperands.push_back(EvalValue::LocalRef(state.mLocals[slot].Type(), slot));
      break;
    }
    case Op::LoadRef: {
      const u32 refSlot = frame.mLocalBase + instruction.mA;
      if (refSlot >= state.mLocals.size()) {
        return fail("internal error: reference parameter slot out of range");
      }
      const EvalValue &reference = state.mLocals[refSlot];
      if (!reference.IsLocalRef() || reference.RefStack() != 0 || reference.RefSlot() >= state.mLocals.size()) {
        return fail("internal error: invalid local reference");
      }
      state.mOperands.push_back(state.mLocals[reference.RefSlot()]);
      break;
    }
    case Op::StoreRef: {
      const u32 refSlot = frame.mLocalBase + instruction.mA;
      if (refSlot >= state.mLocals.size()) {
        return fail("internal error: reference parameter slot out of range");
      }
      const EvalValue &reference = state.mLocals[refSlot];
      if (!reference.IsLocalRef() || reference.RefStack() != 0 || reference.RefSlot() >= state.mLocals.size()) {
        return fail("internal error: invalid local reference");
      }
      EvalValue value = pop();
      if (value.IsLocalRef()) {
        return fail("internal error: storing a reference");
      }
      state.mLocals[reference.RefSlot()] = std::move(value);
      break;
    }
    case Op::Member: {
      EvalValue base = pop();
      EvalValue value =
        base.Root() == kNoRoot
          ? EvalValue::LValue(instruction.mType, base.Address() + instruction.mImm)
          : EvalValue::RootLValue(instruction.mType, base.Root(), base.Address() + instruction.mImm);
      if (instruction.mBitSize > 0) {
        value = EvalValue::Bitfield(value, instruction.mBitOffset, instruction.mBitSize);
      }
      state.mOperands.push_back(value);
      break;
    }
    case Op::Deref: {
      const EvalValue pointer = pop();
      if (pointer.Bits() == 0 && instruction.mA == 0) {
        return fail("null pointer dereference");
      }
      state.mOperands.push_back(EvalValue::LValue(instruction.mType, pointer.Bits()));
      break;
    }
    case Op::AddressOf:
    case Op::Decay: {
      const EvalValue lvalue = pop();
      if (lvalue.Root() != kNoRoot) {
        return fail("the value has no address (it is in a register)");
      }
      state.mOperands.push_back(EvalValue::Scalar(instruction.mType, lvalue.Address()));
      break;
    }
    case Op::Load: {
      const EvalValue lvalue = pop();
      auto value = Load(lvalue, instruction.mLoc);
      if (!value) {
        return std::unexpected(std::move(value.error()));
      }
      state.mOperands.push_back(std::move(*value));
      break;
    }
    case Op::Negate: {
      const EvalValue operand = pop();
      if (mView.Type(instruction.mType).mKind == TypeKind::Float) {
        state.mOperands.push_back(EvalValue::Float(instruction.mType, -operand.AsFloat()));
      } else {
        state.mOperands.push_back(MakeInteger(instruction.mType, 0 - operand.Bits()));
      }
      break;
    }
    case Op::LogicalNot: {
      const EvalValue operand = pop();
      const bool isFloat = mView.Type(operand.Type()).mKind == TypeKind::Float;
      state.mOperands.push_back(
        MakeInteger(instruction.mType, (isFloat ? operand.AsFloat() == 0.0 : operand.Bits() == 0) ? 1 : 0));
      break;
    }
    case Op::BitNot:
      state.mOperands.push_back(MakeInteger(instruction.mType, ~pop().Bits()));
      break;
    case Op::Add:
    case Op::Sub:
    case Op::Mul:
    case Op::Div:
    case Op::Mod:
    case Op::Shl:
    case Op::Shr:
    case Op::BitAnd:
    case Op::BitOr:
    case Op::BitXor:
    case Op::Equal:
    case Op::NotEqual:
    case Op::Less:
    case Op::LessEqual:
    case Op::Greater:
    case Op::GreaterEqual: {
      const EvalValue rhs = pop();
      const EvalValue lhs = pop();
      auto value = EvalBinary(instruction.mOp, lhs, rhs, instruction.mType, instruction.mLoc);
      if (!value) {
        return std::unexpected(std::move(value.error()));
      }
      state.mOperands.push_back(std::move(*value));
      break;
    }
    case Op::PointerAdd:
    case Op::PointerSub: {
      const EvalValue offset = pop();
      const EvalValue pointer = pop();
      const u64 scaled = static_cast<u64>(static_cast<i64>(offset.Bits())) * instruction.mImm;
      const u64 address = instruction.mOp == Op::PointerAdd ? pointer.Bits() + scaled : pointer.Bits() - scaled;
      state.mOperands.push_back(EvalValue::Scalar(instruction.mType, address));
      break;
    }
    case Op::PointerDiff: {
      const EvalValue rhs = pop();
      const EvalValue lhs = pop();
      const i64 bytes = static_cast<i64>(lhs.Bits() - rhs.Bits());
      state.mOperands.push_back(MakeInteger(
        instruction.mType, static_cast<u64>(bytes / static_cast<i64>(std::max<u64>(instruction.mImm, 1)))));
      break;
    }
    case Op::Convert: {
      const EvalValue operand = pop();
      auto value = Convert(operand, instruction.mType, instruction.mLoc);
      if (!value) {
        return std::unexpected(std::move(value.error()));
      }
      state.mOperands.push_back(std::move(*value));
      break;
    }
    case Op::Text: {
      std::optional<EvalValue> length;
      if (instruction.mA == 1) {
        length = pop();
      }
      const EvalValue pointer = pop();
      auto value = EvalText(instruction, pointer, length);
      if (!value) {
        return std::unexpected(std::move(value.error()));
      }
      state.mOperands.push_back(std::move(*value));
      break;
    }
    case Op::ArrayProvider: {
      const EvalValue count = pop();
      const EvalValue pointer = pop();
      const i64 signedCount = static_cast<i64>(count.Bits());
      if (signedCount < 0) {
        return fail(std::format("negative element count {}", signedCount));
      }
      const u64 elementCount = std::min<u64>(static_cast<u64>(signedCount), instruction.mImm);
      const bool truncated = elementCount < static_cast<u64>(signedCount);
      if (elementCount > 0 && pointer.Bits() == 0) {
        return fail(std::format("array of {} elements at null pointer", elementCount));
      }
      state.mOperands.push_back(
        EvalValue::ArrayProvider(instruction.mType, pointer.Bits(), elementCount, truncated));
      break;
    }
    case Op::Pop:
      (void)pop();
      break;
    case Op::Jump:
      frame.mPc = static_cast<u32>(instruction.mImm);
      break;
    case Op::JumpIfFalse: {
      const EvalValue condition = pop();
      if (condition.Bits() == 0) {
        frame.mPc = static_cast<u32>(instruction.mImm);
      }
      break;
    }
    case Op::Return: {
      std::optional<EvalValue> value;
      if (instruction.mA == 1) {
        value = pop();
        if (value->IsLocalRef()) {
          return fail("internal error: returning a reference");
        }
      }
      const u32 localBase = frame.mLocalBase;
      state.mLocals.resize(localBase);
      state.mFrames.pop_back();
      if (value) {
        state.mOperands.push_back(std::move(*value));
      }
      if (state.mFrames.empty()) {
        state.mDone = true;
        return RunResult{ .mStop = RunStop::Done, .mSteps = mStepsUsed - stepsAtStart };
      }
      break;
    }
    case Op::Call: {
      if (state.mFrames.size() >= limits.mMaxDepth) {
        return fail(std::format("call depth exceeded {}", limits.mMaxDepth));
      }
      const BoundView *view = frame.mView;
      if (instruction.mA >= view->mProgram.mFunctions.size()) {
        return fail("internal error: function index out of range");
      }
      const Function &callee = view->mProgram.mFunctions[instruction.mA];
      const u32 function = instruction.mA;
      const u32 entry = callee.mEntry;
      const u32 paramCount = callee.mParamCount;
      const std::vector<TypeId> localTypes = callee.mLocalTypes;
      const u32 localBase = static_cast<u32>(state.mLocals.size());
      if (state.mOperands.size() < paramCount) {
        return fail("internal error: not enough call arguments");
      }
      std::vector<EvalValue> args(paramCount);
      for (u32 i = paramCount; i > 0; --i) {
        args[i - 1] = pop();
      }
      state.mLocals.insert(state.mLocals.end(), args.begin(), args.end());
      for (size_t i = paramCount; i < localTypes.size(); ++i) {
        state.mLocals.push_back(EvalValue::Scalar(localTypes[i], 0));
      }
      state.mFrames.push_back(
        Frame{ .mView = view, .mFunction = function, .mPc = entry, .mLocalBase = localBase });
      break;
    }
    case Op::Fail:
      return fail(frame.mView->mProgram.mConstants[instruction.mA]);
    case Op::Yield: {
      if (!allowYield) {
        return fail("internal error: yield outside a children block");
      }
      const EvalValue value = pop();
      if (value.IsLocalRef()) {
        return fail("internal error: yielding a reference");
      }
      std::optional<u64> address;
      if (instruction.mImm != 1) {
        if (!value.IsLValue() || value.Root() != kNoRoot) {
          return fail("the value has no address (it is in a register)");
        }
        address = value.Address();
      }
      std::optional<std::string> key;
      if (instruction.mA == 1) {
        const EvalValue keyValue = pop();
        auto formatted = Format(keyValue, FormatSpec::Default);
        if (!formatted) {
          return std::unexpected(std::move(formatted.error()));
        }
        key = std::move(*formatted);
      }
      onYield(
        YieldedElement{ .mType = value.Type(), .mAddress = address, .mValue = value, .mKey = std::move(key) });
      ++yields;
      if (yields >= limits.mMaxYields) {
        return RunResult{ .mStop = RunStop::Paused, .mSteps = mStepsUsed - stepsAtStart };
      }
      break;
    }
    }
  }
  state.mDone = true;
  return RunResult{ .mStop = RunStop::Done, .mSteps = mStepsUsed - stepsAtStart };
}

std::expected<RunResult, EvalError>
Evaluator::Run(ProgramState &state,
  const RunLimits &limits,
  const std::function<void(const YieldedElement &)> &onYield) noexcept
{
  mStepsUsed = 0;
  u64 stepsLeft = limits.mBudget;
  return RunWithBudget(state, limits, stepsLeft, onYield, true);
}

Evaluator::Result
Evaluator::Call(u32 function, std::span<const EvalValue> args, u64 budget) noexcept
{
  mStepsUsed = 0;
  ProgramState state = Start(function, args);
  u64 stepsLeft = budget;
  const RunLimits limits{ .mBudget = budget };
  return RunToCompletion(state, limits, stepsLeft);
}

static void
FormatInteger(std::string &out, u64 bits, bool isSigned, u32 size, FormatSpec format) noexcept
{
  switch (format) {
  case FormatSpec::Hex:
    std::format_to(std::back_inserter(out), "0x{:x}", Truncate(bits, size * 8));
    return;
  case FormatSpec::HexUpper:
    std::format_to(std::back_inserter(out), "0x{:X}", Truncate(bits, size * 8));
    return;
  case FormatSpec::Octal:
    std::format_to(std::back_inserter(out), "0{:o}", Truncate(bits, size * 8));
    return;
  case FormatSpec::Binary:
    std::format_to(std::back_inserter(out), "0b{:b}", Truncate(bits, size * 8));
    return;
  default:
    if (isSigned) {
      std::format_to(std::back_inserter(out), "{}", static_cast<i64>(bits));
    } else {
      std::format_to(std::back_inserter(out), "{}", bits);
    }
  }
}

std::expected<std::string, EvalError>
Evaluator::Format(const EvalValue &input, FormatSpec format) noexcept
{
  const BoundType &type = mView.Type(input.Type());
  std::string out;
  if (input.IsLValue()) {
    if (type.mKind == TypeKind::Record || type.mKind == TypeKind::Array) {
      out.append("{");
      out.append(type.mName);
      out.append("}");
      return out;
    }
    auto loaded = Load(input, {});
    if (!loaded) {
      return std::unexpected(std::move(loaded.error()));
    }
    return Format(*loaded, format);
  }

  switch (type.mKind) {
  case TypeKind::Void:
    return std::string{ "void" };
  case TypeKind::Bool:
    if (format == FormatSpec::Default) {
      return std::string{ input.Bits() != 0 ? "true" : "false" };
    }
    FormatInteger(out, input.Bits(), false, 1, format);
    return out;
  case TypeKind::SignedInt:
  case TypeKind::UnsignedInt:
  case TypeKind::Enum: {
    const bool asChar = format == FormatSpec::Character || (type.mIsChar && format == FormatSpec::Default);
    if (asChar) {
      const u32 cp = static_cast<u32>(Truncate(input.Bits(), type.mSize * 8));
      std::string encoded;
      AppendUtf8(cp, encoded);
      FormatInteger(out, input.Bits(), type.mIsSigned, type.mSize, FormatSpec::Default);
      out.append(" '");
      if (cp < 0x20 || cp == 0x7F) {
        std::string quoted;
        AppendQuoted(encoded, false, quoted);
        out.append(quoted.substr(1, quoted.size() - 2));
      } else if (cp == '\'') {
        out.append("\\'");
      } else {
        out.append(encoded);
      }
      out.push_back('\'');
      return out;
    }
    FormatInteger(out, input.Bits(), type.mIsSigned, type.mSize, format);
    return out;
  }
  case TypeKind::Float:
    std::format_to(std::back_inserter(out), "{}", input.AsFloat());
    return out;
  case TypeKind::Pointer:
    std::format_to(std::back_inserter(out), "0x{:x}", input.Bits());
    return out;
  case TypeKind::Text:
    AppendQuoted(TextOf(input), input.Truncated(), out);
    return out;
  case TypeKind::ArrayProvider:
    std::format_to(std::back_inserter(out), "[{}{}]", input.Count(), input.Truncated() ? "+" : "");
    return out;
  case TypeKind::Record:
  case TypeKind::Array:
    out.append(type.mName);
    return out;
  }
  return out;
}

std::string_view
Evaluator::TextOf(const EvalValue &value) const noexcept
{
  assert(value.Kind() == ValueKind::Text);
  const u32 handle = value.TextHandle();
  assert(handle < mTexts.size());
  return mTexts[handle];
}

static std::expected<EvalValue, EvalError>
ThisPointer(const BoundView &view, const std::vector<Root> &roots) noexcept
{
  if (roots.empty() || !roots[0].mAddress) {
    return std::unexpected(EvalError{ .mMessage = "view root has no address", .mLoc = {} });
  }
  return EvalValue::Scalar(view.mThisType, *roots[0].mAddress);
}

std::expected<EvalValue, EvalError>
Evaluator::ThisArgument() const noexcept
{
  return ThisPointer(mView, mRoots);
}

std::expected<std::string, EvalError>
Evaluator::Summary(u64 budget) noexcept
{
  auto thisPointer = ThisPointer(mView, mRoots);
  if (!thisPointer) {
    return std::unexpected(std::move(thisPointer.error()));
  }
  if (mView.mSummaryKind == SummarySpec::Kind::Expression) {
    auto value = Call(mView.mSummaryFunction, std::span{ &*thisPointer, 1 }, budget);
    if (!value) {
      return std::unexpected(std::move(value.error()));
    }
    return Format(*value, mView.mSummaryFormat);
  }

  std::string out;
  for (const auto &part : mView.mSummaryParts) {
    out.append(part.mLiteral);
    if (part.mFunction == kNoFunction) {
      continue;
    }
    auto value = Call(part.mFunction, std::span{ &*thisPointer, 1 }, budget);
    if (!value) {
      return std::unexpected(std::move(value.error()));
    }
    auto formatted = Format(*value, part.mFormat);
    if (!formatted) {
      return std::unexpected(std::move(formatted.error()));
    }
    out.append(*formatted);
  }
  return out;
}

std::expected<ArrayChildren, EvalError>
Evaluator::Children(u64 budget) noexcept
{
  auto thisPointer = ThisPointer(mView, mRoots);
  if (!thisPointer) {
    return std::unexpected(std::move(thisPointer.error()));
  }
  auto value = Call(mView.mChildrenFunction, std::span{ &*thisPointer, 1 }, budget);
  if (!value) {
    return std::unexpected(std::move(value.error()));
  }
  return ArrayChildren{
    .mBase = value->Bits(), .mCount = value->Count(), .mElement = mView.Type(value->Type()).mElement
  };
}

} // namespace mdb::views
