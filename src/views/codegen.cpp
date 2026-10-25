/** LICENSE TEMPLATE */
#include "codegen.h"

// std
#include <optional>
#include <utility>

namespace mdb::views {

u32
CodeGen::Emit(Instruction instruction) noexcept
{
  mView.mProgram.mCode.push_back(instruction);
  return static_cast<u32>(mView.mProgram.mCode.size() - 1);
}

void
CodeGen::PatchTarget(u32 jump, u32 target) noexcept
{
  mView.mProgram.mCode[jump].mImm = target;
}

u32
CodeGen::Here() const noexcept
{
  return static_cast<u32>(mView.mProgram.mCode.size());
}

void
CodeGen::EmitExpr(NodeId id) noexcept
{
  const BoundNode &node = mView.Node(id);
  const auto emit = [&](Op op, u32 a = 0, std::optional<u64> imm = std::nullopt, TypeId type = kNoType) {
    return Emit(Instruction{ .mOp = op,
      .mWidth = node.mWidth,
      .mBitSize = node.mBitSize,
      .mBitOffset = node.mBitOffset,
      .mType = type == kNoType ? node.mType : type,
      .mA = a,
      .mImm = imm.value_or(node.mImm),
      .mFloat = node.mFloat,
      .mLoc = node.mLoc });
  };
  const auto emitOp = [&]() -> std::optional<Op> {
    switch (node.mOp) {
    case BoundOp::IntConst:
    case BoundOp::FloatConst:
    case BoundOp::TextConst:
    case BoundOp::This:
    case BoundOp::LetRef:
    case BoundOp::LogicalAnd:
    case BoundOp::LogicalOr:
    case BoundOp::Ternary:
    case BoundOp::Text:
    case BoundOp::ArrayProvider:
    case BoundOp::Call:
    case BoundOp::LocalRef:
    case BoundOp::LoadRef:
    case BoundOp::Root:
      if (!mError) {
        mError = "internal error: codegen does not handle this node";
      }
      return std::nullopt;
    case BoundOp::Member:
      return Op::Member;
    case BoundOp::Deref:
      return Op::Deref;
    case BoundOp::AddressOf:
      return Op::AddressOf;
    case BoundOp::Load:
      return Op::Load;
    case BoundOp::Decay:
      return Op::Decay;
    case BoundOp::Negate:
      return Op::Negate;
    case BoundOp::LogicalNot:
      return Op::LogicalNot;
    case BoundOp::BitNot:
      return Op::BitNot;
    case BoundOp::Add:
      return Op::Add;
    case BoundOp::Sub:
      return Op::Sub;
    case BoundOp::Mul:
      return Op::Mul;
    case BoundOp::Div:
      return Op::Div;
    case BoundOp::Mod:
      return Op::Mod;
    case BoundOp::Shl:
      return Op::Shl;
    case BoundOp::Shr:
      return Op::Shr;
    case BoundOp::BitAnd:
      return Op::BitAnd;
    case BoundOp::BitOr:
      return Op::BitOr;
    case BoundOp::BitXor:
      return Op::BitXor;
    case BoundOp::Equal:
      return Op::Equal;
    case BoundOp::NotEqual:
      return Op::NotEqual;
    case BoundOp::Less:
      return Op::Less;
    case BoundOp::LessEqual:
      return Op::LessEqual;
    case BoundOp::Greater:
      return Op::Greater;
    case BoundOp::GreaterEqual:
      return Op::GreaterEqual;
    case BoundOp::PointerAdd:
      return Op::PointerAdd;
    case BoundOp::PointerSub:
      return Op::PointerSub;
    case BoundOp::PointerDiff:
      return Op::PointerDiff;
    case BoundOp::Convert:
      return Op::Convert;
    case BoundOp::Local:
      return Op::LoadLocal;
    }
    if (!mError) {
      mError = "internal error: codegen does not handle this node";
    }
    return std::nullopt;
  };

  switch (node.mOp) {
  case BoundOp::IntConst:
    emit(Op::PushInt, 0, node.mImm);
    break;
  case BoundOp::FloatConst:
    emit(Op::PushFloat);
    break;
  case BoundOp::TextConst: {
    const u32 constant = static_cast<u32>(mView.mProgram.mConstants.size());
    mView.mProgram.mConstants.push_back(node.mText);
    emit(Op::PushText, constant);
    break;
  }
  case BoundOp::This:
    emit(Op::LoadLocal, 0, 0, mView.mThisType);
    // The viewed object can be at address 0 (a value held in registers); its bytes come from root 0
    emit(Op::Deref, 1);
    break;
  case BoundOp::LetRef:
    emit(Op::LoadLet, static_cast<u32>(node.mImm));
    break;
  case BoundOp::Local:
    emit(Op::LoadLocal, static_cast<u32>(node.mImm));
    break;
  case BoundOp::LocalRef:
    emit(Op::PushRef, static_cast<u32>(node.mImm));
    break;
  case BoundOp::LoadRef:
    emit(Op::LoadRef, static_cast<u32>(node.mImm));
    break;
  case BoundOp::Root:
    emit(Op::PushRoot, static_cast<u32>(node.mImm));
    break;
  case BoundOp::LogicalAnd: {
    EmitExpr(node.mA);
    const u32 firstFalse = emit(Op::JumpIfFalse);
    EmitExpr(node.mB);
    const u32 secondFalse = emit(Op::JumpIfFalse);
    emit(Op::PushInt, 0, 1);
    const u32 endJump = emit(Op::Jump);
    const u32 falseTarget = Here();
    emit(Op::PushInt, 0, 0);
    const u32 end = Here();
    PatchTarget(firstFalse, falseTarget);
    PatchTarget(secondFalse, falseTarget);
    PatchTarget(endJump, end);
    break;
  }
  case BoundOp::LogicalOr: {
    EmitExpr(node.mA);
    const u32 firstFalse = emit(Op::JumpIfFalse);
    emit(Op::PushInt, 0, 1);
    const u32 endJump = emit(Op::Jump);
    const u32 evaluateRight = Here();
    PatchTarget(firstFalse, evaluateRight);
    EmitExpr(node.mB);
    const u32 secondFalse = emit(Op::JumpIfFalse);
    emit(Op::PushInt, 0, 1);
    const u32 secondEndJump = emit(Op::Jump);
    const u32 falseTarget = Here();
    emit(Op::PushInt, 0, 0);
    const u32 end = Here();
    PatchTarget(secondFalse, falseTarget);
    PatchTarget(endJump, end);
    PatchTarget(secondEndJump, end);
    break;
  }
  case BoundOp::Ternary: {
    EmitExpr(node.mA);
    const u32 elseJump = emit(Op::JumpIfFalse);
    EmitExpr(node.mB);
    const u32 endJump = emit(Op::Jump);
    const u32 elseTarget = Here();
    PatchTarget(elseJump, elseTarget);
    EmitExpr(node.mC);
    PatchTarget(endJump, Here());
    break;
  }
  case BoundOp::Text:
    EmitExpr(node.mA);
    if (node.mB != kNoNode) {
      EmitExpr(node.mB);
    }
    emit(Op::Text, node.mB == kNoNode ? 0 : 1);
    break;
  case BoundOp::ArrayProvider:
    EmitExpr(node.mA);
    EmitExpr(node.mB);
    emit(Op::ArrayProvider);
    break;
  case BoundOp::Call: {
    emit(Op::LoadLocal, 0, 0, mView.mThisType);
    const u32 first = static_cast<u32>(node.mA);
    const u32 count = static_cast<u32>(node.mB);
    for (u32 i = 0; i < count; ++i) {
      EmitExpr(mView.mCallArgs[first + i]);
    }
    emit(Op::Call, static_cast<u32>(node.mImm));
    break;
  }
  default:
    const auto op = emitOp();
    if (!op) {
      break;
    }
    if (node.mA != kNoNode) {
      EmitExpr(node.mA);
    }
    if (node.mB != kNoNode) {
      EmitExpr(node.mB);
    }
    emit(*op);
    break;
  }
}

u32
CodeGen::EmitExprFunction(std::string name, NodeId node) noexcept
{
  const u32 function = static_cast<u32>(mView.mProgram.mFunctions.size());
  mView.mProgram.mFunctions.push_back(Function{ .mName = std::move(name),
    .mEntry = Here(),
    .mParamCount = 1,
    .mLocalTypes = { mView.mThisType },
    .mReturn = mView.Node(node).mType });
  EmitExpr(node);
  Emit(Instruction{ .mOp = Op::Return, .mType = mView.Node(node).mType, .mA = 1, .mLoc = mView.Node(node).mLoc });
  return function;
}

u32
CodeGen::BeginFunction(std::string name, bool canYield) noexcept
{
  const u32 function = static_cast<u32>(mView.mProgram.mFunctions.size());
  mView.mProgram.mFunctions.push_back(Function{ .mName = std::move(name),
    .mEntry = Here(),
    .mParamCount = 1,
    .mLocalTypes = { mView.mThisType },
    .mReturn = kNoType,
    .mCanYield = canYield });
  return function;
}

u32
CodeGen::AddFunction(std::string name, bool canYield) noexcept
{
  const u32 function = static_cast<u32>(mView.mProgram.mFunctions.size());
  mView.mProgram.mFunctions.push_back(Function{ .mName = std::move(name),
    .mParamCount = 1,
    .mLocalTypes = { mView.mThisType },
    .mReturn = kNoType,
    .mCanYield = canYield });
  return function;
}

u32
CodeGen::AddLocal(u32 function, TypeId type) noexcept
{
  auto &locals = mView.mProgram.mFunctions[function].mLocalTypes;
  locals.push_back(type);
  return static_cast<u32>(locals.size() - 1);
}

void
CodeGen::EmitFail(std::string message, SourceLoc loc) noexcept
{
  const u32 constant = static_cast<u32>(mView.mProgram.mConstants.size());
  mView.mProgram.mConstants.push_back(std::move(message));
  Emit(Instruction{ .mOp = Op::Fail, .mA = constant, .mLoc = loc });
}

} // namespace mdb::views
