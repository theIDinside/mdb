/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <common/typedefs.h>
#include <views/diagnostic.h>

// std
#include <limits>
#include <string>
#include <vector>

namespace mdb::views {

using TypeId = u32;
using NodeId = u32;
static constexpr TypeId kNoType = std::numeric_limits<TypeId>::max();
static constexpr NodeId kNoNode = std::numeric_limits<NodeId>::max();
static constexpr u32 kNoFunction = std::numeric_limits<u32>::max();

enum class Op : u8
{
  // Constants. PushText: mA indexes Program::mConstants
  PushInt,
  PushFloat,
  PushText,
  // Root mA, as an lvalue (or bytes without an address). For frame scripts (decision 11). View code reaches the
  // viewed object through its hidden `this` parameter instead (decision 12)
  PushRoot,
  // Let mA: evaluated on first use, then cached for the rest of the run
  LoadLet,
  // Local mA of the current frame
  LoadLocal,
  StoreLocal,
  // Local reference operations. PushRef uses a frame-relative slot; LoadRef and StoreRef use a reference parameter
  PushRef,
  LoadRef,
  StoreRef,
  // Memory, on the top of the stack: same meaning as the BoundOp with the same name.
  // Deref: mA = 1 for the hidden `this`, which is not checked for null
  Member,
  Deref,
  AddressOf,
  Load,
  Decay,
  // Pop one or two operands, push the result
  Negate,
  LogicalNot,
  BitNot,
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Shl,
  Shr,
  BitAnd,
  BitOr,
  BitXor,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  PointerAdd,
  PointerSub,
  PointerDiff,
  Convert,
  // Builtins. Text: mA = 1 if a length was pushed after the pointer
  Text,
  ArrayProvider,
  // Control flow
  Pop,
  Jump,
  JumpIfFalse,
  // Call function mA of the current frame's view. Its mParamCount arguments are on the stack, first argument
  // pushed first. For view functions, the first argument is `this`
  Call,
  // Return from the current function. mA = 1 if a value is returned (popped, and pushed for the caller)
  Return,
  // Fail with Program::mConstants[mA]
  Fail,
  // Pop the value (and, if mA = 1, the key, which was pushed first) and hand them to the runtime
  Yield,
};

struct Instruction
{
  Op mOp;
  u8 mWidth{ 1 };          // Text: code unit size
  u16 mBitSize{ 0 };       // Member: bitfields
  u32 mBitOffset{ 0 };     // Member: bitfields
  TypeId mType{ kNoType }; // the type of the result
  u32 mA{ 0 };             // see Op
  u64 mImm{ 0 };           // as BoundNode::mImm. Jump, JumpIfFalse: the target instruction
  double mFloat{ 0.0 };    // PushFloat
  SourceLoc mLoc;
};

struct Function
{
  // "summary", "let hdr", "children", "size", or the name after `fn`
  std::string mName;
  u32 mEntry{ 0 };                 // index into Program::mCode
  u32 mParamCount{ 0 };            // the first mParamCount locals are the parameters. Local 0 is `this`
  std::vector<TypeId> mLocalTypes; // one per slot
  TypeId mReturn{ kNoType };       // kNoType: returns nothing
  bool mCanYield{ false };
};

struct Program
{
  std::vector<Instruction> mCode;
  std::vector<std::string> mConstants;
  std::vector<Function> mFunctions;
};

} // namespace mdb::views
