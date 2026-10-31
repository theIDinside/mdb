#include <cstring>
#include <gtest/gtest.h>
#include <map>
#include <views/codegen.h>
#include <views/evaluator.h>

using namespace mdb;
using namespace mdb::views;

namespace {

// Target memory: a set of mapped byte ranges. Reads outside of them fail.
class FakeMemory final : public MemoryReader
{
  std::map<u64, std::vector<u8>> mRegions;

public:
  int mReads{ 0 };

  void
  Map(u64 address, std::vector<u8> bytes)
  {
    mRegions[address] = std::move(bytes);
  }

  template <typename T>
  void
  MapValue(u64 address, const T &value)
  {
    std::vector<u8> bytes(sizeof(T));
    std::memcpy(bytes.data(), &value, sizeof(T));
    Map(address, std::move(bytes));
  }

  bool
  Read(u64 address, std::span<u8> out) noexcept final
  {
    ++mReads;
    for (const auto &[start, bytes] : mRegions) {
      if (address >= start && address - start + out.size() <= bytes.size()) {
        std::memcpy(out.data(), bytes.data() + (address - start), out.size());
        return true;
      }
    }
    return false;
  }
};

// Builds bound views by hand, the way the binder would.
struct Builder
{
  BoundView mView;
  TypeId mI32;
  TypeId mU32;
  TypeId mI64;
  TypeId mU64;
  TypeId mBool;
  TypeId mChar16;
  TypeId mText;

  Builder()
  {
    mI32 = Type({ .mKind = TypeKind::SignedInt, .mSize = 4, .mIsSigned = true, .mName = "int" });
    mU32 = Type({ .mKind = TypeKind::UnsignedInt, .mSize = 4, .mName = "unsigned int" });
    mI64 = Type({ .mKind = TypeKind::SignedInt, .mSize = 8, .mIsSigned = true, .mName = "long" });
    mU64 = Type({ .mKind = TypeKind::UnsignedInt, .mSize = 8, .mName = "unsigned long" });
    mBool = Type({ .mKind = TypeKind::Bool, .mSize = 1, .mName = "bool" });
    mChar16 = Type({ .mKind = TypeKind::UnsignedInt, .mSize = 2, .mIsChar = true, .mName = "char16_t" });
    mText = Type({ .mKind = TypeKind::Text, .mName = "text" });
    mView.mRootType = Type({ .mKind = TypeKind::Record, .mSize = 16, .mName = "Root" });
    mView.mThisType = PointerTo(mView.mRootType);
  }

  TypeId
  Type(BoundType type)
  {
    mView.mTypes.push_back(std::move(type));
    return static_cast<TypeId>(mView.mTypes.size() - 1);
  }

  TypeId
  PointerTo(TypeId element)
  {
    return Type({ .mKind = TypeKind::Pointer, .mSize = 8, .mElement = element, .mName = "ptr" });
  }

  NodeId
  Node(BoundNode node)
  {
    mView.mNodes.push_back(std::move(node));
    return static_cast<NodeId>(mView.mNodes.size() - 1);
  }

  NodeId
  This()
  {
    return Node({ .mOp = BoundOp::This, .mType = mView.mRootType });
  }

  // this->member at `offset`, loaded
  NodeId
  LoadMember(TypeId type, u64 offset)
  {
    const NodeId member = Node({ .mOp = BoundOp::Member, .mType = type, .mA = This(), .mImm = offset });
    return Node({ .mOp = BoundOp::Load, .mType = type, .mA = member });
  }

  NodeId
  Int(TypeId type, u64 value)
  {
    return Node({ .mOp = BoundOp::IntConst, .mType = type, .mImm = value });
  }
};

template <typename... T>
std::vector<u8>
Bytes(T... values)
{
  std::vector<u8> bytes;
  (
    [&](auto value) {
      const auto *p = reinterpret_cast<const u8 *>(&value);
      bytes.insert(bytes.end(), p, p + sizeof(value));
    }(values),
    ...);
  return bytes;
}

std::expected<EvalValue, EvalError>
CallNode(Evaluator &eval, Builder &builder, NodeId node, u64 thisAddress = 0)
{
  CodeGen codegen{ builder.mView };
  const u32 function = codegen.EmitExprFunction("test", node);
  const EvalValue thisPointer = EvalValue::Scalar(builder.mView.mThisType, thisAddress);
  return eval.Call(function, std::span{ &thisPointer, 1 }, kDefaultBudget);
}

} // namespace

TEST(ViewsCodeGen, LoadedMemberOfThis)
{
  Builder b;
  const NodeId member = b.LoadMember(b.mU32, 8);
  CodeGen codegen{ b.mView };
  const u32 function = codegen.EmitExprFunction("summary", member);
  const auto &fn = b.mView.mProgram.mFunctions[function];
  ASSERT_EQ(fn.mParamCount, 1);
  ASSERT_EQ(fn.mLocalTypes, (std::vector<TypeId>{ b.mView.mThisType }));
  const auto &code = b.mView.mProgram.mCode;
  ASSERT_EQ(code.size(), 5);
  EXPECT_EQ(code[0].mOp, Op::LoadLocal);
  EXPECT_EQ(code[0].mA, 0);
  EXPECT_EQ(code[1].mOp, Op::Deref);
  EXPECT_EQ(code[1].mA, 1);
  EXPECT_EQ(code[2].mOp, Op::Member);
  EXPECT_EQ(code[2].mImm, 8);
  EXPECT_EQ(code[3].mOp, Op::Load);
  EXPECT_EQ(code[4].mOp, Op::Return);
  EXPECT_EQ(code[4].mA, 1);
}

TEST(ViewsCodeGen, CallsAndFailInstructions)
{
  Builder b;
  const NodeId first = b.Int(b.mI32, 11);
  const NodeId second = b.Int(b.mI32, 22);
  b.mView.mCallArgs = { first, second };
  const NodeId call = b.Node(BoundNode{ .mOp = BoundOp::Call, .mType = b.mI32, .mA = 0, .mB = 2, .mImm = 7 });
  CodeGen codegen{ b.mView };
  const u32 function = codegen.EmitExprFunction("caller", call);
  EXPECT_EQ(function, 0);
  ASSERT_EQ(b.mView.mProgram.mCode.size(), 5);
  EXPECT_EQ(b.mView.mProgram.mCode[0].mOp, Op::LoadLocal);
  EXPECT_EQ(b.mView.mProgram.mCode[0].mA, 0);
  EXPECT_EQ(b.mView.mProgram.mCode[0].mType, b.mView.mThisType);
  EXPECT_EQ(b.mView.mProgram.mCode[1].mOp, Op::PushInt);
  EXPECT_EQ(b.mView.mProgram.mCode[1].mImm, 11);
  EXPECT_EQ(b.mView.mProgram.mCode[2].mOp, Op::PushInt);
  EXPECT_EQ(b.mView.mProgram.mCode[2].mImm, 22);
  EXPECT_EQ(b.mView.mProgram.mCode[3].mOp, Op::Call);
  EXPECT_EQ(b.mView.mProgram.mCode[3].mA, 7);
  EXPECT_EQ(b.mView.mProgram.mCode[3].mType, b.mI32);

  codegen.EmitFail("missing return", SourceLoc{ 4, 9 });
  ASSERT_EQ(b.mView.mProgram.mConstants.size(), 1);
  EXPECT_EQ(b.mView.mProgram.mConstants[0], "missing return");
  EXPECT_EQ(b.mView.mProgram.mCode.back().mOp, Op::Fail);
  EXPECT_EQ(b.mView.mProgram.mCode.back().mA, 0);
}

TEST(ViewsCodeGen, LogicalAndUsesShortCircuitJumps)
{
  Builder b;
  const NodeId lhs = b.Int(b.mBool, 1);
  const NodeId rhs = b.Int(b.mBool, 0);
  const NodeId node = b.Node({ .mOp = BoundOp::LogicalAnd, .mType = b.mBool, .mA = lhs, .mB = rhs });
  CodeGen codegen{ b.mView };
  codegen.EmitExprFunction("summary", node);
  const auto &code = b.mView.mProgram.mCode;
  std::vector<u32> jumps;
  for (u32 i = 0; i < code.size(); ++i) {
    if (code[i].mOp == Op::JumpIfFalse) {
      jumps.push_back(i);
    }
  }
  ASSERT_EQ(jumps.size(), 2);
  EXPECT_EQ(code[jumps[0]].mImm, code[jumps[1]].mImm);
  EXPECT_EQ(code[code[jumps[0]].mImm].mOp, Op::PushInt);
  EXPECT_EQ(code[code[jumps[0]].mImm].mImm, 0);
  const u32 endJump = jumps[1] + 2;
  ASSERT_LT(endJump, code.size());
  EXPECT_EQ(code[endJump].mOp, Op::Jump);
  EXPECT_EQ(code[endJump].mImm, code.size() - 1);
}

TEST(ViewsCodeGen, LogicalOrAndTernaryUseControlFlow)
{
  Builder b;
  const NodeId a = b.LoadMember(b.mBool, 0);
  const NodeId rhs = b.LoadMember(b.mBool, 1);
  const NodeId either = b.Node({ .mOp = BoundOp::LogicalOr, .mType = b.mBool, .mA = a, .mB = rhs });
  CodeGen codegen{ b.mView };
  codegen.EmitExprFunction("or", either);
  const auto &orCode = b.mView.mProgram.mCode;
  std::vector<u32> conditionalJumps;
  std::vector<u32> jumps;
  std::vector<u32> falseConstants;
  for (u32 i = 0; i < orCode.size(); ++i) {
    if (orCode[i].mOp == Op::JumpIfFalse) {
      conditionalJumps.push_back(i);
    } else if (orCode[i].mOp == Op::Jump) {
      jumps.push_back(i);
    } else if (orCode[i].mOp == Op::PushInt && orCode[i].mImm == 0) {
      falseConstants.push_back(i);
    }
  }
  ASSERT_EQ(conditionalJumps.size(), 2);
  ASSERT_EQ(falseConstants.size(), 1);
  const u32 firstConditional = conditionalJumps[0];
  const u32 rightEntry = static_cast<u32>(orCode[firstConditional].mImm);
  EXPECT_EQ(orCode[rightEntry].mOp, Op::LoadLocal);
  EXPECT_EQ(orCode[firstConditional + 1].mOp, Op::PushInt);
  EXPECT_EQ(orCode[firstConditional + 1].mImm, 1);
  EXPECT_EQ(orCode[firstConditional + 2].mOp, Op::Jump);
  const u32 returnIndex = falseConstants[0] + 1;
  EXPECT_EQ(orCode[conditionalJumps[1]].mImm, falseConstants[0]);
  EXPECT_EQ(orCode[returnIndex].mOp, Op::Return);
  for (u32 jump : jumps) {
    EXPECT_EQ(orCode[jump].mImm, returnIndex);
  }

  Builder t;
  const NodeId cond = t.Int(t.mBool, 1);
  const NodeId yes = t.Int(t.mI32, 10);
  const NodeId no = t.Int(t.mI32, 20);
  const NodeId ternary = t.Node({ .mOp = BoundOp::Ternary, .mType = t.mI32, .mA = cond, .mB = yes, .mC = no });
  CodeGen ternaryGen{ t.mView };
  ternaryGen.EmitExprFunction("ternary", ternary);
  const auto &ternaryCode = t.mView.mProgram.mCode;
  ASSERT_EQ(ternaryCode[1].mOp, Op::JumpIfFalse);
  EXPECT_EQ(ternaryCode[ternaryCode[1].mImm].mOp, Op::PushInt);
  EXPECT_EQ(ternaryCode[3].mOp, Op::Jump);
  EXPECT_EQ(ternaryCode[3].mImm, ternaryCode.size() - 1);
}

TEST(ViewsCodeGen, LetTextAndBuiltinInstructions)
{
  Builder b;
  const NodeId letRef = b.Node({ .mOp = BoundOp::LetRef, .mType = b.mI32, .mImm = 3 });
  CodeGen gen{ b.mView };
  gen.EmitExprFunction("let", letRef);
  EXPECT_EQ(b.mView.mProgram.mCode[0].mOp, Op::LoadLet);
  EXPECT_EQ(b.mView.mProgram.mCode[0].mA, 3);

  const NodeId textConst = b.Node({ .mOp = BoundOp::TextConst, .mType = b.mText, .mText = "hello" });
  const u32 textFn = gen.EmitExprFunction("text", textConst);
  const auto &textPush = b.mView.mProgram.mCode[b.mView.mProgram.mFunctions[textFn].mEntry];
  ASSERT_EQ(textPush.mOp, Op::PushText);
  EXPECT_EQ(b.mView.mProgram.mConstants[textPush.mA], "hello");

  const TypeId charPtr = b.PointerTo(b.mChar16);
  const NodeId pointer = b.Int(charPtr, 0x1000);
  const NodeId length = b.Int(b.mI64, 2);
  const NodeId withLength =
    b.Node({ .mOp = BoundOp::Text, .mType = b.mText, .mA = pointer, .mB = length, .mImm = 16, .mWidth = 2 });
  const u32 withLengthFn = gen.EmitExprFunction("with length", withLength);
  const u32 withLengthEntry = b.mView.mProgram.mFunctions[withLengthFn].mEntry;
  EXPECT_EQ(b.mView.mProgram.mCode[withLengthEntry + 2].mOp, Op::Text);
  EXPECT_EQ(b.mView.mProgram.mCode[withLengthEntry + 2].mA, 1);

  const NodeId nulText =
    b.Node({ .mOp = BoundOp::Text, .mType = b.mText, .mA = pointer, .mImm = 16, .mWidth = 2 });
  const u32 nulTextFn = gen.EmitExprFunction("nul text", nulText);
  const u32 nulTextEntry = b.mView.mProgram.mFunctions[nulTextFn].mEntry;
  EXPECT_EQ(b.mView.mProgram.mCode[nulTextEntry + 1].mOp, Op::Text);
  EXPECT_EQ(b.mView.mProgram.mCode[nulTextEntry + 1].mA, 0);
}

TEST(ViewsEval, MembersOfTheRootComeFromItsBytes)
{
  Builder b;
  const NodeId length = b.LoadMember(b.mU32, 8);
  FakeMemory memory;
  const auto root = Bytes(u64{ 0x1000 }, u32{ 7 }, u32{ 0 });
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = root };
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };
  auto value = CallNode(eval, b, length, 0x5000);
  ASSERT_TRUE(value.has_value()) << value.error().mMessage;
  EXPECT_EQ(value->Bits(), 7);
  EXPECT_EQ(memory.mReads, 0);
}

TEST(ViewsEval, DerefAndPointerArithmetic)
{
  Builder b;
  // struct Header { u32 length; u32 capacity; }; Root { Header* hdr; }
  const TypeId header = b.Type({ .mKind = TypeKind::Record, .mSize = 8, .mName = "Header" });
  const TypeId headerPtr = b.PointerTo(header);
  const NodeId hdr = b.LoadMember(headerPtr, 0);
  const NodeId deref = b.Node({ .mOp = BoundOp::Deref, .mType = header, .mA = hdr });
  const NodeId lengthLValue = b.Node({ .mOp = BoundOp::Member, .mType = b.mU32, .mA = deref, .mImm = 0 });
  const NodeId length = b.Node({ .mOp = BoundOp::Load, .mType = b.mU32, .mA = lengthLValue });

  // (int*)(hdr + 1)
  const NodeId one = b.Int(b.mI64, 1);
  const NodeId afterHeader =
    b.Node({ .mOp = BoundOp::PointerAdd, .mType = headerPtr, .mA = hdr, .mB = one, .mImm = 8 });
  const TypeId intPtr = b.PointerTo(b.mI32);
  const NodeId elements = b.Node({ .mOp = BoundOp::Convert, .mType = intPtr, .mA = afterHeader });
  // elements[2]
  const NodeId two = b.Int(b.mI64, 2);
  const NodeId third =
    b.Node({ .mOp = BoundOp::PointerAdd, .mType = intPtr, .mA = elements, .mB = two, .mImm = 4 });
  const NodeId thirdValue = b.Node({ .mOp = BoundOp::Load,
    .mType = b.mI32,
    .mA = b.Node({ .mOp = BoundOp::Deref, .mType = b.mI32, .mA = third }) });

  FakeMemory memory;
  memory.Map(0x1000, Bytes(u32{ 3 }, u32{ 8 }, i32{ 10 }, i32{ -20 }, i32{ 30 }));
  const auto root = Bytes(u64{ 0x1000 }, u64{ 0 });
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = root };
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };

  auto len = CallNode(eval, b, length, 0x5000);
  ASSERT_TRUE(len.has_value()) << len.error().mMessage;
  EXPECT_EQ(len->Bits(), 3);

  auto address = CallNode(eval, b, elements, 0x5000);
  ASSERT_TRUE(address.has_value());
  EXPECT_EQ(address->Bits(), 0x1008);

  auto value = CallNode(eval, b, thirdValue, 0x5000);
  ASSERT_TRUE(value.has_value()) << value.error().mMessage;
  EXPECT_EQ(static_cast<i64>(value->Bits()), 30);

  // pointer difference in elements
  const NodeId diff =
    b.Node({ .mOp = BoundOp::PointerDiff, .mType = b.mI64, .mA = third, .mB = elements, .mImm = 4 });
  Evaluator eval2{ b.mView, memory, std::span{ &rootInfo, 1 } };
  auto difference = CallNode(eval2, b, diff, 0x5000);
  ASSERT_TRUE(difference.has_value());
  EXPECT_EQ(static_cast<i64>(difference->Bits()), 2);
}

TEST(ViewsEval, NullDerefAndUnreadableMemoryAreErrors)
{
  Builder b;
  const TypeId intPtr = b.PointerTo(b.mI32);
  const NodeId pointer = b.LoadMember(intPtr, 0);
  const NodeId value = b.Node({ .mOp = BoundOp::Load,
    .mType = b.mI32,
    .mA = b.Node({ .mOp = BoundOp::Deref, .mType = b.mI32, .mA = pointer, .mLoc = { 3, 7 } }) });

  FakeMemory memory;
  const auto nullRoot = Bytes(u64{ 0 }, u64{ 0 });
  const Root nullRootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = nullRoot };
  Evaluator nullEval{ b.mView, memory, std::span{ &nullRootInfo, 1 } };
  auto nullResult = CallNode(nullEval, b, value, 0x5000);
  ASSERT_FALSE(nullResult.has_value());
  EXPECT_EQ(nullResult.error().mMessage, "null pointer dereference");
  EXPECT_EQ(nullResult.error().mLoc.mLine, 3);

  const auto badRoot = Bytes(u64{ 0xdead0000 }, u64{ 0 });
  const Root badRootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = badRoot };
  Evaluator badEval{ b.mView, memory, std::span{ &badRootInfo, 1 } };
  auto badResult = CallNode(badEval, b, value, 0x5000);
  ASSERT_FALSE(badResult.has_value());
  EXPECT_NE(badResult.error().mMessage.find("0xdead0000"), std::string::npos);
}

TEST(ViewsEval, Bitfields)
{
  Builder b;
  // bits 3..7 (5 bits) of the byte at offset 1, signed and unsigned
  const NodeId unsignedField = b.Node({ .mOp = BoundOp::Load,
    .mType = b.mU32,
    .mA = b.Node(
      { .mOp = BoundOp::Member, .mType = b.mU32, .mA = b.This(), .mImm = 1, .mBitOffset = 3, .mBitSize = 5 }) });
  const NodeId signedField = b.Node({ .mOp = BoundOp::Load,
    .mType = b.mI32,
    .mA = b.Node(
      { .mOp = BoundOp::Member, .mType = b.mI32, .mA = b.This(), .mImm = 1, .mBitOffset = 3, .mBitSize = 5 }) });

  FakeMemory memory;
  // 0b11110'000 at offset 1: field value 0b11110 = 30 unsigned, -2 signed
  const std::vector<u8> root{ 0x00, 0b11110000, 0x00, 0x00 };
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = root };
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };
  EXPECT_EQ(CallNode(eval, b, unsignedField, 0x5000)->Bits(), 30);
  EXPECT_EQ(static_cast<i64>(CallNode(eval, b, signedField, 0x5000)->Bits()), -2);
}

TEST(ViewsEval, BitfieldOffsetIsNormalized)
{
  Builder b;
  const NodeId field = b.Node({ .mOp = BoundOp::Load,
    .mType = b.mU32,
    .mA = b.Node({ .mOp = BoundOp::Member, .mType = b.mU32, .mA = b.This(), .mBitOffset = 37, .mBitSize = 5 }) });

  std::vector<u8> root(9, 0);
  constexpr u32 value = 0b10110;
  for (u32 bit = 0; bit < 5; ++bit) {
    if ((value & (1U << bit)) != 0) {
      root[(37 + bit) / 8] |= static_cast<u8>(1U << ((37 + bit) % 8));
    }
  }
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = root };
  FakeMemory memory;
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };

  auto result = CallNode(eval, b, field, 0x5000);
  ASSERT_TRUE(result.has_value()) << result.error().mMessage;
  EXPECT_EQ(result->Bits(), value);
}

TEST(ViewsEval, SixtyFourBitFieldAtOffsetNeedsNineByteRead)
{
  Builder b;
  const auto makeField = [&](TypeId type) {
    return b.Node({ .mOp = BoundOp::Load,
      .mType = type,
      .mA = b.Node({ .mOp = BoundOp::Member, .mType = type, .mA = b.This(), .mBitOffset = 3, .mBitSize = 64 }) });
  };
  const NodeId unsignedField = makeField(b.mU64);
  const NodeId signedField = makeField(b.mI64);

  constexpr u64 value = 0x8000'0000'0000'0001ULL;
  std::vector<u8> root(9, 0);
  for (u32 bit = 0; bit < 64; ++bit) {
    if ((value & (1ULL << bit)) != 0) {
      root[(3 + bit) / 8] |= static_cast<u8>(1U << ((3 + bit) % 8));
    }
  }
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = root };
  FakeMemory memory;
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };

  auto unsignedResult = CallNode(eval, b, unsignedField, 0x5000);
  ASSERT_TRUE(unsignedResult.has_value()) << unsignedResult.error().mMessage;
  EXPECT_EQ(unsignedResult->Bits(), value);
  auto signedResult = CallNode(eval, b, signedField, 0x5000);
  ASSERT_TRUE(signedResult.has_value()) << signedResult.error().mMessage;
  EXPECT_EQ(static_cast<i64>(signedResult->Bits()), -0x7FFF'FFFF'FFFF'FFFFLL);
}

TEST(ViewsEval, ArithmeticAndComparisons)
{
  Builder b;
  FakeMemory memory;
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0, .mBytes = {} };
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };
  const auto eval64 = [&](BoundOp op, TypeId type, u64 lhs, u64 rhs) {
    const NodeId node = b.Node({ .mOp = op, .mType = type, .mA = b.Int(type, lhs), .mB = b.Int(type, rhs) });
    Evaluator e{ b.mView, memory, std::span{ &rootInfo, 1 } };
    return CallNode(e, b, node);
  };
  EXPECT_EQ(static_cast<i64>(eval64(BoundOp::Sub, b.mI64, 3, 5)->Bits()), -2);
  EXPECT_EQ(static_cast<i64>(eval64(BoundOp::Div, b.mI64, static_cast<u64>(-9), 2)->Bits()), -4);
  EXPECT_EQ(eval64(BoundOp::Shl, b.mU64, 1, 40)->Bits(), 1ULL << 40);
  EXPECT_FALSE(eval64(BoundOp::Div, b.mI64, 1, 0).has_value());
  // signed vs unsigned comparisons
  const NodeId lessSigned = b.Node(
    { .mOp = BoundOp::Less, .mType = b.mBool, .mA = b.Int(b.mI64, static_cast<u64>(-1)), .mB = b.Int(b.mI64, 1) });
  Evaluator e{ b.mView, memory, std::span{ &rootInfo, 1 } };
  EXPECT_EQ(CallNode(e, b, lessSigned)->Bits(), 1);
  EXPECT_EQ(eval64(BoundOp::Less, b.mU64, static_cast<u64>(-1), 1)->Bits(), 0);

  // Narrowing conversion truncates and sign extends
  const TypeId i8 = b.Type({ .mKind = TypeKind::SignedInt, .mSize = 1, .mIsSigned = true, .mName = "i8" });
  const NodeId narrowed = b.Node({ .mOp = BoundOp::Convert, .mType = i8, .mA = b.Int(b.mI64, 0x1FF) });
  Evaluator e2{ b.mView, memory, std::span{ &rootInfo, 1 } };
  EXPECT_EQ(static_cast<i64>(CallNode(e2, b, narrowed)->Bits()), -1);
}

TEST(ViewsEval, LetsAreEvaluatedOnce)
{
  Builder b;
  const TypeId intPtr = b.PointerTo(b.mI32);
  const NodeId pointer = b.LoadMember(intPtr, 0);
  const NodeId target = b.Node({ .mOp = BoundOp::Load,
    .mType = b.mI32,
    .mA = b.Node({ .mOp = BoundOp::Deref, .mType = b.mI32, .mA = pointer }) });
  CodeGen codegen{ b.mView };
  b.mView.mLetFunctions.push_back(codegen.EmitExprFunction("let 0", target));
  const NodeId ref = b.Node({ .mOp = BoundOp::LetRef, .mType = b.mI32, .mImm = 0 });
  const NodeId sum = b.Node({ .mOp = BoundOp::Add, .mType = b.mI32, .mA = ref, .mB = ref });

  FakeMemory memory;
  memory.MapValue(0x2000, i32{ 21 });
  const auto root = Bytes(u64{ 0x2000 }, u64{ 0 });
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = root };
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };
  auto result = CallNode(eval, b, sum, 0x5000);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->Bits(), 42);
  EXPECT_EQ(memory.mReads, 1);
}

TEST(ViewsEval, TextWithLength)
{
  Builder b;
  const TypeId char16Ptr = b.PointerTo(b.mChar16);
  const NodeId data = b.LoadMember(char16Ptr, 0);
  const NodeId length = b.LoadMember(b.mU32, 8);
  const NodeId lengthI64 = b.Node({ .mOp = BoundOp::Convert, .mType = b.mI64, .mA = length });
  const NodeId text =
    b.Node({ .mOp = BoundOp::Text, .mType = b.mText, .mA = data, .mB = lengthI64, .mImm = 4096, .mWidth = 2 });
  const NodeId limited =
    b.Node({ .mOp = BoundOp::Text, .mType = b.mText, .mA = data, .mB = lengthI64, .mImm = 2, .mWidth = 2 });

  FakeMemory memory;
  // "Hé😀"
  memory.Map(0x3000, Bytes(u16{ 'H' }, u16{ 0xE9 }, u16{ 0xD83D }, u16{ 0xDE00 }));
  const auto root = Bytes(u64{ 0x3000 }, u32{ 4 }, u32{ 0 });
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = root };
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };
  auto value = CallNode(eval, b, text, 0x5000);
  ASSERT_TRUE(value.has_value()) << value.error().mMessage;
  EXPECT_EQ(eval.TextOf(*value), "H\xC3\xA9\xF0\x9F\x98\x80");
  EXPECT_FALSE(value->Truncated());
  EXPECT_EQ(*eval.Format(*value, FormatSpec::Default), "\"H\xC3\xA9\xF0\x9F\x98\x80\"");

  auto truncated = CallNode(eval, b, limited, 0x5000);
  ASSERT_TRUE(truncated.has_value());
  EXPECT_EQ(*eval.Format(*truncated, FormatSpec::Default), "\"H\xC3\xA9\"...");

  // Zero length text with a null pointer reads nothing and is empty
  const auto emptyRoot = Bytes(u64{ 0 }, u32{ 0 }, u32{ 0 });
  const Root emptyRootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = emptyRoot };
  Evaluator emptyEval{ b.mView, memory, std::span{ &emptyRootInfo, 1 } };
  auto empty = CallNode(emptyEval, b, text, 0x5000);
  ASSERT_TRUE(empty.has_value()) << empty.error().mMessage;
  EXPECT_EQ(*emptyEval.Format(*empty, FormatSpec::Default), "\"\"");
}

TEST(ViewsEval, NulTerminatedTextStopsAtUnmappedMemory)
{
  Builder b;
  const TypeId charType =
    b.Type({ .mKind = TypeKind::SignedInt, .mSize = 1, .mIsChar = true, .mIsSigned = true, .mName = "char" });
  const NodeId data = b.LoadMember(b.PointerTo(charType), 0);
  const NodeId text = b.Node({ .mOp = BoundOp::Text, .mType = b.mText, .mA = data, .mImm = 4096, .mWidth = 1 });

  FakeMemory memory;
  // A terminated string that ends right before a page boundary (the next page is unmapped)
  std::vector<u8> page(16, 0);
  std::memcpy(page.data(), "hello", 6);
  memory.Map(0x4000 - 16, page);
  const auto root = Bytes(u64{ 0x4000 - 16 }, u64{ 0 });
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = root };
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };
  auto value = CallNode(eval, b, text, 0x5000);
  ASSERT_TRUE(value.has_value()) << value.error().mMessage;
  EXPECT_EQ(eval.TextOf(*value), "hello");

  // An unterminated string running into unmapped memory shows what could be read
  std::vector<u8> unterminated(16, 'x');
  memory.Map(0x8000 - 16, unterminated);
  const auto root2 = Bytes(u64{ 0x8000 - 16 }, u64{ 0 });
  const Root rootInfo2{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = root2 };
  Evaluator eval2{ b.mView, memory, std::span{ &rootInfo2, 1 } };
  auto partial = CallNode(eval2, b, text, 0x5000);
  ASSERT_TRUE(partial.has_value()) << partial.error().mMessage;
  EXPECT_EQ(eval2.TextOf(*partial), std::string(16, 'x'));
  EXPECT_TRUE(partial->Truncated());
}

TEST(ViewsEval, TextHandlesCoverConstantsAndDecodedText)
{
  Builder b;
  const NodeId constant = b.Node({ .mOp = BoundOp::TextConst, .mType = b.mText, .mText = "literal" });
  const TypeId char16Ptr = b.PointerTo(b.mChar16);
  const NodeId data = b.LoadMember(char16Ptr, 0);
  const NodeId length = b.LoadMember(b.mU32, 8);
  const NodeId decoded =
    b.Node({ .mOp = BoundOp::Text, .mType = b.mText, .mA = data, .mB = length, .mImm = 16, .mWidth = 2 });

  CodeGen codegen{ b.mView };
  b.mView.mSummaryKind = SummarySpec::Kind::Template;
  b.mView.mSummaryParts.push_back({ .mLiteral = "constant=" });
  b.mView.mSummaryParts.back().mFunction = codegen.EmitExprFunction("constant", constant);
  b.mView.mSummaryParts.push_back({ .mLiteral = " decoded=" });
  b.mView.mSummaryParts.back().mFunction = codegen.EmitExprFunction("decoded", decoded);

  FakeMemory memory;
  memory.Map(0x3000, Bytes(u16{ 'o' }, u16{ 'k' }));
  const auto root = Bytes(u64{ 0x3000 }, u32{ 2 }, u32{ 0 });
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = root };
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };

  auto summary = eval.Summary();
  ASSERT_TRUE(summary.has_value()) << summary.error().mMessage;
  EXPECT_EQ(*summary, "constant=\"literal\" decoded=\"ok\"");
}

TEST(ViewsEval, ArrayChildrenAndSummaries)
{
  Builder b;
  const TypeId intPtr = b.PointerTo(b.mI32);
  const TypeId provider =
    b.Type({ .mKind = TypeKind::ArrayProvider, .mElement = b.mI32, .mName = "array of int" });
  const NodeId data = b.LoadMember(intPtr, 0);
  const NodeId count = b.Node({ .mOp = BoundOp::Convert, .mType = b.mI64, .mA = b.LoadMember(b.mU32, 8) });
  b.mView.mChildrenKind = ChildrenSpec::Kind::Expression;
  const NodeId childrenNode =
    b.Node({ .mOp = BoundOp::ArrayProvider, .mType = provider, .mA = data, .mB = count, .mImm = 100 });
  CodeGen codegen{ b.mView };
  b.mView.mChildrenFunction = codegen.EmitExprFunction("children", childrenNode);

  b.mView.mSummaryKind = SummarySpec::Kind::Template;
  b.mView.mSummaryParts.push_back({ .mLiteral = "len=" });
  b.mView.mSummaryParts.back().mFunction = codegen.EmitExprFunction("summary", count);
  b.mView.mSummaryParts.push_back({ .mLiteral = " data=" });
  b.mView.mSummaryParts.back().mFunction = codegen.EmitExprFunction("summary", data);
  b.mView.mSummaryParts.push_back({ .mLiteral = " hex=", .mFormat = FormatSpec::Hex });
  b.mView.mSummaryParts.back().mFunction = codegen.EmitExprFunction("summary", count);

  FakeMemory memory;
  const auto root = Bytes(u64{ 0x1000 }, u32{ 255 }, u32{ 0 });
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = root };
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };
  auto children = eval.Children();
  ASSERT_TRUE(children.has_value()) << children.error().mMessage;
  EXPECT_EQ(children->mBase, 0x1000);
  EXPECT_EQ(children->mCount, 100); // clamped to the limit
  EXPECT_EQ(children->mElement, b.mI32);

  auto summary = eval.Summary();
  ASSERT_TRUE(summary.has_value()) << summary.error().mMessage;
  EXPECT_EQ(*summary, "len=255 data=0x1000 hex=0xff");

  // A non-empty array at a null pointer is an error
  const auto nullRoot = Bytes(u64{ 0 }, u32{ 3 }, u32{ 0 });
  const Root nullRootInfo{ .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = nullRoot };
  Evaluator nullEval{ b.mView, memory, std::span{ &nullRootInfo, 1 } };
  EXPECT_FALSE(nullEval.Children().has_value());
}

TEST(ViewsEval, FormatsCharacters)
{
  Builder b;
  FakeMemory memory;
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0, .mBytes = {} };
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };
  EvalValue value = EvalValue::Scalar(b.mChar16, 0x65E5);
  EXPECT_EQ(*eval.Format(value, FormatSpec::Default), "26085 '\xE6\x97\xA5'");
  value = EvalValue::Scalar(b.mChar16, '\n');
  EXPECT_EQ(*eval.Format(value, FormatSpec::Default), "10 '\\n'");
  EXPECT_EQ(*eval.Format(value, FormatSpec::Hex), "0xa");
}

TEST(ViewsEval, RootsWithAndWithoutAddresses)
{
  Builder b;
  const TypeId secondRootType = b.Type({ .mKind = TypeKind::Record, .mSize = 4, .mName = "RegisterRoot" });
  const auto firstRootBytes = Bytes(u32{ 17 });
  const auto secondRootBytes = Bytes(u32{ 29 });
  const Root roots[] = {
    { .mType = b.mView.mRootType, .mAddress = 0x5000, .mBytes = firstRootBytes },
    { .mType = secondRootType, .mAddress = std::nullopt, .mBytes = secondRootBytes },
  };
  FakeMemory memory;
  Evaluator eval{ b.mView, memory, std::span{ roots } };

  const NodeId firstRoot = b.Node({ .mOp = BoundOp::Root, .mType = b.mView.mRootType, .mImm = 0 });
  const NodeId firstValue = b.Node({ .mOp = BoundOp::Load,
    .mType = b.mU32,
    .mA = b.Node({ .mOp = BoundOp::Member, .mType = b.mU32, .mA = firstRoot }) });
  auto first = CallNode(eval, b, firstValue, 0x5000);
  ASSERT_TRUE(first.has_value()) << first.error().mMessage;
  EXPECT_EQ(first->Bits(), 17);

  const NodeId secondRoot = b.Node({ .mOp = BoundOp::Root, .mType = secondRootType, .mImm = 1 });
  const NodeId secondMember = b.Node({ .mOp = BoundOp::Member, .mType = b.mU32, .mA = secondRoot });
  const NodeId secondValue = b.Node({ .mOp = BoundOp::Load, .mType = b.mU32, .mA = secondMember });
  auto second = CallNode(eval, b, secondValue, 0x5000);
  ASSERT_TRUE(second.has_value()) << second.error().mMessage;
  EXPECT_EQ(second->Bits(), 29);

  const TypeId pointerType = b.PointerTo(b.mU32);
  const NodeId addressOf = b.Node({ .mOp = BoundOp::AddressOf, .mType = pointerType, .mA = secondMember });
  auto address = CallNode(eval, b, addressOf, 0x5000);
  ASSERT_FALSE(address.has_value());
  EXPECT_NE(address.error().mMessage.find("has no address"), std::string::npos);
}

TEST(ViewsEval, ViewedObjectAtAddressZeroIsReadFromItsBytes)
{
  // A value held in registers has address 0, but its bytes are known
  Builder b;
  const NodeId length = b.LoadMember(b.mU32, 8);
  FakeMemory memory;
  const auto root = Bytes(u64{ 0 }, u32{ 7 }, u32{ 0 });
  const Root rootInfo{ .mType = b.mView.mRootType, .mAddress = 0, .mBytes = root };
  Evaluator eval{ b.mView, memory, std::span{ &rootInfo, 1 } };
  auto value = CallNode(eval, b, length, 0);
  ASSERT_TRUE(value.has_value()) << value.error().mMessage;
  EXPECT_EQ(value->Bits(), 7);
  EXPECT_EQ(memory.mReads, 0);
}

TEST(ViewsEval, RunBudgetStopsAndCallReportsExhaustion)
{
  Builder b;
  b.mView.mProgram.mFunctions.push_back(Function{ .mName = "loop", .mEntry = 0 });
  b.mView.mProgram.mCode.push_back(Instruction{ .mOp = Op::Jump, .mImm = 0 });
  FakeMemory memory;
  Evaluator eval{ b.mView, memory, std::span<const Root>{} };

  ProgramState state = eval.Start(0, std::span<const EvalValue>{});
  const auto noYield = [](const YieldedElement &) {};
  auto result = eval.Run(state, RunLimits{ .mBudget = 1000 }, noYield);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->mStop, RunStop::Budget);
  EXPECT_EQ(result->mSteps, 1000);

  auto call = eval.Call(0, std::span<const EvalValue>{}, 1000);
  ASSERT_FALSE(call.has_value());
  EXPECT_EQ(call.error().mMessage, "stopped after 1000 steps");
}

TEST(ViewsEval, YieldPausesAndCopiesProgramState)
{
  Builder b;
  const TypeId intPointer = b.PointerTo(b.mI32);
  CodeGen codegen{ b.mView };
  const u32 function = codegen.BeginFunction("children", true);
  const u32 dataSlot = codegen.AddLocal(function, intPointer);
  const u32 indexSlot = codegen.AddLocal(function, b.mI64);
  EXPECT_EQ(dataSlot, 1);
  EXPECT_EQ(indexSlot, 2);

  const NodeId dataValue = b.Node({ .mOp = BoundOp::IntConst, .mType = intPointer, .mImm = 0x1000 });
  codegen.EmitExpr(dataValue);
  codegen.Emit(Instruction{ .mOp = Op::StoreLocal, .mA = dataSlot });
  const NodeId zero = b.Node({ .mOp = BoundOp::IntConst, .mType = b.mI64, .mImm = 0 });
  codegen.EmitExpr(zero);
  codegen.Emit(Instruction{ .mOp = Op::StoreLocal, .mA = indexSlot });

  const auto local = [&](u32 slot, TypeId type) {
    return b.Node(BoundNode{ .mOp = BoundOp::Local, .mType = type, .mImm = slot });
  };
  const auto integer = [&](u64 value) {
    return b.Node(BoundNode{ .mOp = BoundOp::IntConst, .mType = b.mI64, .mImm = value });
  };
  const u32 loopTop = codegen.Here();
  const NodeId lessThanSix =
    b.Node(BoundNode{ .mOp = BoundOp::Less, .mType = b.mBool, .mA = local(indexSlot, b.mI64), .mB = integer(6) });
  codegen.EmitExpr(lessThanSix);
  const u32 exit = codegen.Emit(Instruction{ .mOp = Op::JumpIfFalse });

  const NodeId remainder =
    b.Node(BoundNode{ .mOp = BoundOp::Mod, .mType = b.mI64, .mA = local(indexSlot, b.mI64), .mB = integer(2) });
  const NodeId even =
    b.Node(BoundNode{ .mOp = BoundOp::Equal, .mType = b.mBool, .mA = remainder, .mB = integer(0) });
  codegen.EmitExpr(even);
  const u32 skipYield = codegen.Emit(Instruction{ .mOp = Op::JumpIfFalse });

  const NodeId elementPointer = b.Node(BoundNode{ .mOp = BoundOp::PointerAdd,
    .mType = intPointer,
    .mA = local(dataSlot, intPointer),
    .mB = local(indexSlot, b.mI64),
    .mImm = 4 });
  const NodeId element = b.Node(BoundNode{ .mOp = BoundOp::Deref, .mType = b.mI32, .mA = elementPointer });
  codegen.EmitExpr(element);
  codegen.Emit(Instruction{ .mOp = Op::Yield });
  codegen.PatchTarget(skipYield, codegen.Here());

  const NodeId incremented =
    b.Node(BoundNode{ .mOp = BoundOp::Add, .mType = b.mI64, .mA = local(indexSlot, b.mI64), .mB = integer(1) });
  codegen.EmitExpr(incremented);
  codegen.Emit(Instruction{ .mOp = Op::StoreLocal, .mA = indexSlot });
  codegen.Emit(Instruction{ .mOp = Op::Jump, .mImm = loopTop });
  codegen.PatchTarget(exit, codegen.Here());
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 0 });

  b.mView.mChildrenFunction = function;
  FakeMemory memory;
  memory.Map(0x1000, Bytes(i32{ 10 }, i32{ 20 }, i32{ 30 }, i32{ 40 }, i32{ 50 }, i32{ 60 }));
  Evaluator eval{ b.mView, memory, std::span<const Root>{} };
  const EvalValue thisPointer = EvalValue::Scalar(b.mView.mThisType, 0);
  ProgramState state = eval.Start(function, std::span{ &thisPointer, 1 });
  std::vector<YieldedElement> yielded;
  const auto collect = [&](const YieldedElement &element) { yielded.push_back(element); };

  auto firstPage = eval.Run(state, RunLimits{ .mMaxYields = 2, .mBudget = 1000 }, collect);
  ASSERT_TRUE(firstPage.has_value()) << firstPage.error().mMessage;
  EXPECT_EQ(firstPage->mStop, RunStop::Paused);
  ASSERT_EQ(yielded.size(), 2);
  EXPECT_EQ(yielded[0].mAddress, std::optional<u64>{ 0x1000 });
  EXPECT_EQ(yielded[1].mAddress, std::optional<u64>{ 0x1008 });
  EXPECT_TRUE(state.mOperands.empty());

  ProgramState pausedCopy = state;
  yielded.clear();
  auto secondPage = eval.Run(state, RunLimits{ .mMaxYields = 1, .mBudget = 1000 }, collect);
  ASSERT_TRUE(secondPage.has_value()) << secondPage.error().mMessage;
  EXPECT_EQ(secondPage->mStop, RunStop::Paused);
  ASSERT_EQ(yielded.size(), 1);
  EXPECT_EQ(yielded[0].mAddress, std::optional<u64>{ 0x1010 });
  EXPECT_TRUE(state.mOperands.empty());

  yielded.clear();
  auto copiedPage = eval.Run(pausedCopy, RunLimits{ .mMaxYields = 1, .mBudget = 1000 }, collect);
  ASSERT_TRUE(copiedPage.has_value()) << copiedPage.error().mMessage;
  EXPECT_EQ(copiedPage->mStop, RunStop::Paused);
  ASSERT_EQ(yielded.size(), 1);
  EXPECT_EQ(yielded[0].mAddress, std::optional<u64>{ 0x1010 });
  EXPECT_TRUE(pausedCopy.mOperands.empty());
}

TEST(ViewsEval, ComputedYieldPausesAndCopiesProgramState)
{
  Builder b;
  CodeGen codegen{ b.mView };
  const u32 function = codegen.BeginFunction("children", true);
  const u64 values[]{ 11, 22, 33 };
  for (const u64 value : values) {
    const NodeId constant = b.Node(BoundNode{ .mOp = BoundOp::IntConst, .mType = b.mI64, .mImm = value });
    codegen.EmitExpr(constant);
    codegen.Emit(Instruction{ .mOp = Op::Yield, .mImm = 1 });
  }
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 0 });
  b.mView.mChildrenFunction = function;

  FakeMemory memory;
  Evaluator eval{ b.mView, memory, std::span<const Root>{} };
  const EvalValue thisPointer = EvalValue::Scalar(b.mView.mThisType, 0);
  ProgramState state = eval.Start(function, std::span{ &thisPointer, 1 });
  std::vector<YieldedElement> yielded;
  const auto collect = [&](const YieldedElement &element) { yielded.push_back(element); };

  auto first = eval.Run(state, RunLimits{ .mMaxYields = 1, .mBudget = 1000 }, collect);
  ASSERT_TRUE(first.has_value()) << first.error().mMessage;
  ASSERT_EQ(yielded.size(), 1);
  EXPECT_EQ(yielded[0].mValue.Bits(), 11);
  EXPECT_FALSE(yielded[0].mAddress.has_value());
  EXPECT_TRUE(state.mOperands.empty());

  ProgramState pausedCopy = state;
  yielded.clear();
  auto resumed = eval.Run(state, RunLimits{ .mMaxYields = 2, .mBudget = 1000 }, collect);
  ASSERT_TRUE(resumed.has_value()) << resumed.error().mMessage;
  ASSERT_EQ(yielded.size(), 2);
  EXPECT_EQ(yielded[0].mValue.Bits(), 22);
  EXPECT_EQ(yielded[1].mValue.Bits(), 33);
  EXPECT_FALSE(yielded[0].mAddress.has_value());
  EXPECT_FALSE(yielded[1].mAddress.has_value());
  EXPECT_TRUE(state.mOperands.empty());

  yielded.clear();
  auto copied = eval.Run(pausedCopy, RunLimits{ .mMaxYields = 2, .mBudget = 1000 }, collect);
  ASSERT_TRUE(copied.has_value()) << copied.error().mMessage;
  ASSERT_EQ(yielded.size(), 2);
  EXPECT_EQ(yielded[0].mValue.Bits(), 22);
  EXPECT_EQ(yielded[1].mValue.Bits(), 33);
  EXPECT_TRUE(pausedCopy.mOperands.empty());
}

TEST(ViewsEval, CallRejectsYield)
{
  Builder b;
  b.mView.mProgram.mFunctions.push_back(
    Function{ .mName = "children", .mParamCount = 1, .mLocalTypes = { b.mView.mThisType }, .mCanYield = true });
  b.mView.mProgram.mCode.push_back(Instruction{ .mOp = Op::Yield });
  FakeMemory memory;
  Evaluator eval{ b.mView, memory, std::span<const Root>{} };
  const EvalValue thisPointer = EvalValue::Scalar(b.mView.mThisType, 0);
  auto result = eval.Call(0, std::span{ &thisPointer, 1 }, 1000);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().mMessage, "internal error: yield outside a children block");
}

TEST(ViewsEval, RecursiveCallsWalkAndCount)
{
  Builder b;
  const TypeId nodeType = b.Type(BoundType{ .mKind = TypeKind::Record, .mSize = 24, .mName = "Node" });
  const TypeId nodePointer = b.PointerTo(nodeType);
  CodeGen codegen{ b.mView };

  const u32 walk = codegen.BeginFunction("walk", true);
  codegen.AddLocal(walk, nodePointer);
  b.mView.mProgram.mFunctions[walk].mParamCount = 2;
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = nodePointer, .mA = 1 });
  const u32 returnEmpty = codegen.Emit(Instruction{ .mOp = Op::JumpIfFalse });

  // walk(left)
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = b.mView.mThisType, .mA = 0 });
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = nodePointer, .mA = 1 });
  codegen.Emit(Instruction{ .mOp = Op::Deref, .mType = nodeType });
  codegen.Emit(Instruction{ .mOp = Op::Member, .mType = nodePointer, .mImm = 0 });
  codegen.Emit(Instruction{ .mOp = Op::Load, .mType = nodePointer });
  codegen.Emit(Instruction{ .mOp = Op::Call, .mA = walk });

  // yield node->key
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = nodePointer, .mA = 1 });
  codegen.Emit(Instruction{ .mOp = Op::Deref, .mType = nodeType });
  codegen.Emit(Instruction{ .mOp = Op::Member, .mType = b.mI32, .mImm = 16 });
  codegen.Emit(Instruction{ .mOp = Op::Load, .mType = b.mI32 });
  codegen.Emit(Instruction{ .mOp = Op::Yield, .mImm = 1 });

  // walk(right)
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = b.mView.mThisType, .mA = 0 });
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = nodePointer, .mA = 1 });
  codegen.Emit(Instruction{ .mOp = Op::Deref, .mType = nodeType });
  codegen.Emit(Instruction{ .mOp = Op::Member, .mType = nodePointer, .mImm = 8 });
  codegen.Emit(Instruction{ .mOp = Op::Load, .mType = nodePointer });
  codegen.Emit(Instruction{ .mOp = Op::Call, .mA = walk });
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 0 });
  codegen.PatchTarget(returnEmpty, codegen.Here());
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 0 });

  const u32 count = codegen.BeginFunction("count", false);
  codegen.AddLocal(count, nodePointer);
  const u32 leftCountSlot = codegen.AddLocal(count, b.mI64);
  b.mView.mProgram.mFunctions[count].mParamCount = 2;
  b.mView.mProgram.mFunctions[count].mReturn = b.mI64;
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = nodePointer, .mA = 1 });
  const u32 returnZero = codegen.Emit(Instruction{ .mOp = Op::JumpIfFalse });
  const auto emitCountCall = [&](u64 offset) {
    codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = b.mView.mThisType, .mA = 0 });
    codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = nodePointer, .mA = 1 });
    codegen.Emit(Instruction{ .mOp = Op::Deref, .mType = nodeType });
    codegen.Emit(Instruction{ .mOp = Op::Member, .mType = nodePointer, .mImm = offset });
    codegen.Emit(Instruction{ .mOp = Op::Load, .mType = nodePointer });
    codegen.Emit(Instruction{ .mOp = Op::Call, .mType = b.mI64, .mA = count });
  };
  emitCountCall(0);
  codegen.Emit(Instruction{ .mOp = Op::StoreLocal, .mA = leftCountSlot });
  emitCountCall(8);
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = b.mI64, .mA = leftCountSlot });
  codegen.Emit(Instruction{ .mOp = Op::Add, .mType = b.mI64 });
  codegen.Emit(Instruction{ .mOp = Op::PushInt, .mType = b.mI64, .mImm = 1 });
  codegen.Emit(Instruction{ .mOp = Op::Add, .mType = b.mI64 });
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 1 });
  codegen.PatchTarget(returnZero, codegen.Here());
  codegen.Emit(Instruction{ .mOp = Op::PushInt, .mType = b.mI64, .mImm = 0 });
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 1 });
  b.mView.mChildrenFunction = walk;

  constexpr u64 base = 0x8000;
  FakeMemory memory;
  for (u64 i = 0; i < 7; ++i) {
    const u64 address = base + i * 24;
    const u64 left = i == 0 ? 0 : address - 24;
    memory.Map(address, Bytes(left, u64{ 0 }, static_cast<i32>(i + 1), u32{ 0 }));
  }
  Evaluator eval{ b.mView, memory, std::span<const Root>{} };
  const EvalValue startArgs[]{ EvalValue::Scalar(b.mView.mThisType, 0),
    EvalValue::Scalar(nodePointer, base + 6 * 24) };
  ProgramState state = eval.Start(walk, startArgs);
  std::vector<u64> keys;
  const auto collect = [&](const YieldedElement &element) { keys.push_back(element.mValue.Bits()); };
  auto firstPage = eval.Run(state, RunLimits{ .mMaxYields = 2, .mBudget = 10000 }, collect);
  ASSERT_TRUE(firstPage.has_value()) << firstPage.error().mMessage;
  EXPECT_EQ(firstPage->mStop, RunStop::Paused);
  ASSERT_EQ(keys, (std::vector<u64>{ 1, 2 }));
  EXPECT_GE(state.mFrames.size(), 3);
  EXPECT_TRUE(state.mOperands.empty());

  ProgramState pausedCopy = state;
  keys.clear();
  auto resumed = eval.Run(state, RunLimits{ .mBudget = 10000 }, collect);
  ASSERT_TRUE(resumed.has_value()) << resumed.error().mMessage;
  EXPECT_EQ(resumed->mStop, RunStop::Done);
  EXPECT_EQ(keys, (std::vector<u64>{ 3, 4, 5, 6, 7 }));
  EXPECT_TRUE(state.mOperands.empty());

  keys.clear();
  auto copied = eval.Run(pausedCopy, RunLimits{ .mBudget = 10000 }, collect);
  ASSERT_TRUE(copied.has_value()) << copied.error().mMessage;
  EXPECT_EQ(copied->mStop, RunStop::Done);
  EXPECT_EQ(keys, (std::vector<u64>{ 3, 4, 5, 6, 7 }));
  EXPECT_TRUE(pausedCopy.mOperands.empty());

  const EvalValue countArgs[]{ startArgs[0], startArgs[1] };
  auto total = eval.Call(count, countArgs, 10000);
  ASSERT_TRUE(total.has_value()) << total.error().mMessage;
  EXPECT_EQ(total->Bits(), 7);
}

TEST(ViewsEval, RecursiveCallDepthIsLimited)
{
  Builder b;
  const TypeId nodeType = b.Type(BoundType{ .mKind = TypeKind::Record, .mSize = 24, .mName = "Node" });
  const TypeId nodePointer = b.PointerTo(nodeType);
  CodeGen codegen{ b.mView };
  const u32 walk = codegen.BeginFunction("walk", true);
  codegen.AddLocal(walk, nodePointer);
  b.mView.mProgram.mFunctions[walk].mParamCount = 2;
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = nodePointer, .mA = 1 });
  const u32 jump = codegen.Emit(Instruction{ .mOp = Op::JumpIfFalse });
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = b.mView.mThisType, .mA = 0 });
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = nodePointer, .mA = 1 });
  codegen.Emit(Instruction{ .mOp = Op::Deref, .mType = nodeType });
  codegen.Emit(Instruction{ .mOp = Op::Member, .mType = nodePointer, .mImm = 0 });
  codegen.Emit(Instruction{ .mOp = Op::Load, .mType = nodePointer });
  codegen.Emit(Instruction{ .mOp = Op::Call, .mA = walk });
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 0 });
  codegen.PatchTarget(jump, codegen.Here());
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 0 });
  constexpr u64 address = 0x9000;
  FakeMemory memory;
  memory.Map(address, Bytes(address, address, i32{ 1 }, u32{ 0 }));
  Evaluator eval{ b.mView, memory, std::span<const Root>{} };
  const EvalValue args[]{ EvalValue::Scalar(b.mView.mThisType, 0), EvalValue::Scalar(nodePointer, address) };
  ProgramState state = eval.Start(walk, args);
  const auto noYield = [](const YieldedElement &) {};
  auto result = eval.Run(state, RunLimits{ .mBudget = 100000 }, noYield);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().mMessage, "call depth exceeded 1024");
  EXPECT_EQ(state.mFrames.size(), 1024);
}

TEST(ViewsEval, FailInstructionUsesProgramConstant)
{
  Builder b;
  b.mView.mProgram.mFunctions.push_back(
    Function{ .mName = "fail", .mParamCount = 1, .mLocalTypes = { b.mView.mThisType } });
  b.mView.mProgram.mConstants.push_back("function failed");
  b.mView.mProgram.mCode.push_back(Instruction{ .mOp = Op::Fail, .mA = 0, .mLoc = { 3, 5 } });
  FakeMemory memory;
  Evaluator eval{ b.mView, memory, std::span<const Root>{} };
  const EvalValue thisPointer = EvalValue::Scalar(b.mView.mThisType, 0);
  auto result = eval.Call(0, std::span{ &thisPointer, 1 }, 1000);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().mMessage, "function failed");
  EXPECT_EQ(result.error().mLoc.mLine, 3);
  EXPECT_EQ(result.error().mLoc.mColumn, 5);
}

TEST(ViewsEval, ReferenceParameterAccumulatesRecursively)
{
  Builder b;
  const TypeId nodeType = b.Type(BoundType{ .mKind = TypeKind::Record, .mSize = 24, .mName = "Node" });
  const TypeId nodePointer = b.PointerTo(nodeType);
  CodeGen codegen{ b.mView };

  const u32 sum = codegen.BeginFunction("sum", false);
  codegen.AddLocal(sum, nodePointer);
  codegen.AddLocal(sum, b.mU64);
  b.mView.mProgram.mFunctions[sum].mParamCount = 3;
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = nodePointer, .mA = 1 });
  const u32 done = codegen.Emit(Instruction{ .mOp = Op::JumpIfFalse });

  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = nodePointer, .mA = 1 });
  codegen.Emit(Instruction{ .mOp = Op::Deref, .mType = nodeType });
  codegen.Emit(Instruction{ .mOp = Op::Member, .mType = b.mU64, .mImm = 16 });
  codegen.Emit(Instruction{ .mOp = Op::Load, .mType = b.mU64 });
  codegen.Emit(Instruction{ .mOp = Op::LoadRef, .mType = b.mU64, .mA = 2 });
  codegen.Emit(Instruction{ .mOp = Op::Add, .mType = b.mU64 });
  codegen.Emit(Instruction{ .mOp = Op::StoreRef, .mA = 2 });

  const auto emitRecursiveCall = [&](u64 offset) {
    codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = b.mView.mThisType, .mA = 0 });
    codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = nodePointer, .mA = 1 });
    codegen.Emit(Instruction{ .mOp = Op::Deref, .mType = nodeType });
    codegen.Emit(Instruction{ .mOp = Op::Member, .mType = nodePointer, .mImm = offset });
    codegen.Emit(Instruction{ .mOp = Op::Load, .mType = nodePointer });
    codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = b.mU64, .mA = 2 });
    codegen.Emit(Instruction{ .mOp = Op::Call, .mA = sum });
  };
  emitRecursiveCall(0);
  emitRecursiveCall(8);
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 0 });
  codegen.PatchTarget(done, codegen.Here());
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 0 });

  const u32 total = codegen.BeginFunction("total", false);
  codegen.AddLocal(total, nodePointer);
  const u32 totalSlot = codegen.AddLocal(total, b.mU64);
  b.mView.mProgram.mFunctions[total].mParamCount = 2;
  b.mView.mProgram.mFunctions[total].mReturn = b.mU64;
  codegen.Emit(Instruction{ .mOp = Op::PushInt, .mType = b.mU64, .mImm = 0 });
  codegen.Emit(Instruction{ .mOp = Op::StoreLocal, .mA = totalSlot });
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = b.mView.mThisType, .mA = 0 });
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = nodePointer, .mA = 1 });
  codegen.Emit(Instruction{ .mOp = Op::PushRef, .mA = totalSlot });
  codegen.Emit(Instruction{ .mOp = Op::Call, .mA = sum });
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = b.mU64, .mA = totalSlot });
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 1 });

  constexpr u64 root = 0xA000;
  constexpr u64 left = 0xA018;
  constexpr u64 right = 0xA030;
  FakeMemory memory;
  memory.Map(root, Bytes(left, right, u64{ 5 }));
  memory.Map(left, Bytes(u64{ 0 }, u64{ 0 }, u64{ 7 }));
  memory.Map(right, Bytes(u64{ 0 }, u64{ 0 }, u64{ 11 }));
  Evaluator eval{ b.mView, memory, std::span<const Root>{} };
  const EvalValue args[]{ EvalValue::Scalar(b.mView.mThisType, 0), EvalValue::Scalar(nodePointer, root) };
  auto result = eval.Call(total, args, 10000);
  ASSERT_TRUE(result.has_value()) << result.error().mMessage;
  EXPECT_EQ(result->Bits(), 23);
}

TEST(ViewsEval, ReferenceParameterSurvivesYieldAndStateCopy)
{
  Builder b;
  CodeGen codegen{ b.mView };
  const u32 yielding = codegen.BeginFunction("yielding", true);
  codegen.AddLocal(yielding, b.mU64);
  b.mView.mProgram.mFunctions[yielding].mParamCount = 2;
  for (u64 i = 0; i < 3; ++i) {
    codegen.Emit(Instruction{ .mOp = Op::LoadRef, .mType = b.mU64, .mA = 1 });
    codegen.Emit(Instruction{ .mOp = Op::PushInt, .mType = b.mU64, .mImm = 1 });
    codegen.Emit(Instruction{ .mOp = Op::Add, .mType = b.mU64 });
    codegen.Emit(Instruction{ .mOp = Op::StoreRef, .mA = 1 });
    codegen.Emit(Instruction{ .mOp = Op::LoadRef, .mType = b.mU64, .mA = 1 });
    codegen.Emit(Instruction{ .mOp = Op::Yield, .mImm = 1 });
  }
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 0 });

  const u32 children = codegen.BeginFunction("children", true);
  const u32 totalSlot = codegen.AddLocal(children, b.mU64);
  codegen.Emit(Instruction{ .mOp = Op::PushInt, .mType = b.mU64, .mImm = 10 });
  codegen.Emit(Instruction{ .mOp = Op::StoreLocal, .mA = totalSlot });
  codegen.Emit(Instruction{ .mOp = Op::LoadLocal, .mType = b.mView.mThisType, .mA = 0 });
  codegen.Emit(Instruction{ .mOp = Op::PushRef, .mA = totalSlot });
  codegen.Emit(Instruction{ .mOp = Op::Call, .mA = yielding });
  codegen.Emit(Instruction{ .mOp = Op::Return, .mA = 0 });
  b.mView.mChildrenFunction = children;

  FakeMemory memory;
  Evaluator eval{ b.mView, memory, std::span<const Root>{} };
  const EvalValue thisPointer = EvalValue::Scalar(b.mView.mThisType, 0);
  ProgramState state = eval.Start(children, std::span{ &thisPointer, 1 });
  std::vector<u64> values;
  const auto collect = [&](const YieldedElement &element) { values.push_back(element.mValue.Bits()); };
  auto first = eval.Run(state, RunLimits{ .mMaxYields = 1, .mBudget = 1000 }, collect);
  ASSERT_TRUE(first.has_value()) << first.error().mMessage;
  EXPECT_EQ(first->mStop, RunStop::Paused);
  ASSERT_EQ(values, (std::vector<u64>{ 11 }));
  ASSERT_EQ(state.mLocals.size(), 4);
  EXPECT_TRUE(state.mLocals[3].IsLocalRef());
  EXPECT_EQ(state.mLocals[3].RefStack(), 0);
  EXPECT_EQ(state.mLocals[3].RefSlot(), totalSlot);
  EXPECT_TRUE(state.mOperands.empty());

  ProgramState pausedCopy = state;
  values.clear();
  auto resumed = eval.Run(state, RunLimits{ .mMaxYields = 2, .mBudget = 1000 }, collect);
  ASSERT_TRUE(resumed.has_value()) << resumed.error().mMessage;
  EXPECT_EQ(resumed->mStop, RunStop::Done);
  EXPECT_EQ(values, (std::vector<u64>{ 12, 13 }));
  EXPECT_TRUE(state.mOperands.empty());

  values.clear();
  auto copied = eval.Run(pausedCopy, RunLimits{ .mMaxYields = 2, .mBudget = 1000 }, collect);
  ASSERT_TRUE(copied.has_value()) << copied.error().mMessage;
  EXPECT_EQ(copied->mStop, RunStop::Done);
  EXPECT_EQ(values, (std::vector<u64>{ 12, 13 }));
  EXPECT_TRUE(pausedCopy.mOperands.empty());
}

TEST(ViewsEval, ShortCircuitAndDoesNotEvaluateFalseBranch)
{
  Builder b;
  const TypeId boolPointer = b.PointerTo(b.mBool);
  const NodeId nullDereference = b.Node({ .mOp = BoundOp::Load,
    .mType = b.mBool,
    .mA = b.Node({ .mOp = BoundOp::Deref, .mType = b.mBool, .mA = b.Int(boolPointer, 0) }) });
  const NodeId node =
    b.Node({ .mOp = BoundOp::LogicalAnd, .mType = b.mBool, .mA = b.Int(b.mBool, 0), .mB = nullDereference });
  FakeMemory memory;
  Evaluator eval{ b.mView, memory, std::span<const Root>{} };
  auto value = CallNode(eval, b, node);
  ASSERT_TRUE(value.has_value()) << value.error().mMessage;
  EXPECT_EQ(value->Bits(), 0);
}
