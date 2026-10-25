/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <common/typedefs.h>
#include <views/ast.h>
#include <views/bytecode.h>
#include <views/diagnostic.h>

// std
#include <optional>
#include <string>
#include <vector>

namespace mdb::sym {
class Type;
}

namespace mdb::views {

// The bound IR is what a view looks like after it has been resolved against one concrete type: every member
// access is a byte offset, every type is known. It deliberately knows nothing about sym::Type beyond carrying an
// opaque pointer to it, so that it can be evaluated (and tested) without any debug information around.

enum class TypeKind : u8
{
  Void,
  Bool,
  SignedInt,
  UnsignedInt,
  Float,
  Enum,
  Pointer,
  Array,
  Record,
  // Decoded text, produced by text(), cstr(), utf16() etc
  Text,
  // A child provider, produced by array()
  ArrayProvider,
};

struct BoundType
{
  TypeKind mKind{ TypeKind::Void };
  u32 mSize{ 0 };
  // Pointee for pointers, element for arrays and providers
  TypeId mElement{ kNoType };
  // Element count for arrays
  u32 mCount{ 0 };
  // Integers that represent characters (char, char16_t, ...) - they're displayed as characters and can be decoded
  // as text.
  bool mIsChar{ false };
  bool mIsSigned{ false };
  // The DWARF type, when there is one. Needed to create sym::Value's of this type (e.g. elements of array()).
  sym::Type *mDwarf{ nullptr };
  std::string mName;

  [[nodiscard]] constexpr bool
  IsInteger() const noexcept
  {
    return mKind == TypeKind::Bool || mKind == TypeKind::SignedInt || mKind == TypeKind::UnsignedInt ||
           mKind == TypeKind::Enum;
  }

  [[nodiscard]] constexpr bool
  IsArithmetic() const noexcept
  {
    return IsInteger() || mKind == TypeKind::Float;
  }

  [[nodiscard]] constexpr bool
  IsScalar() const noexcept
  {
    return IsArithmetic() || mKind == TypeKind::Pointer;
  }
};

enum class BoundOp : u8
{
  IntConst,
  FloatConst,
  TextConst,
  // The object being viewed, as an lvalue
  This,
  // The value of let binding `mA`
  LetRef,
  // A local variable at slot mImm
  Local,
  // A reference to a local at slot mImm
  LocalRef,
  // The value referenced by a reference parameter at slot mImm
  LoadRef,
  // lvalue `mA` + `mImm` bytes, possibly a bitfield
  Member,
  // pointer rvalue `mA` -> lvalue
  Deref,
  // lvalue `mA` -> pointer rvalue
  AddressOf,
  // lvalue `mA` -> rvalue
  Load,
  // array lvalue `mA` -> pointer to its first element
  Decay,
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
  LogicalAnd,
  LogicalOr,
  // mA ? mB : mC
  Ternary,
  // pointer mA +/- integer mB, scaled by mImm (element size)
  PointerAdd,
  PointerSub,
  // (pointer mA - pointer mB) / mImm
  PointerDiff,
  // convert rvalue mA to the node's type
  Convert,
  // Call function mImm with arguments in BoundView::mCallArgs[mA ... mA + mB)
  Call,
  // Root mImm, supplied by the caller of the program
  Root,
  // text(ptr = mA, length = mB or kNoNode for NUL terminated), mWidth = code unit size, mImm = limit in code units
  Text,
  // array(ptr = mA, count = mB), mImm = limit
  ArrayProvider,
};

struct BoundNode
{
  BoundOp mOp;
  TypeId mType{ kNoType };
  NodeId mA{ kNoNode };
  NodeId mB{ kNoNode };
  NodeId mC{ kNoNode };
  u64 mImm{ 0 };
  double mFloat{ 0.0 };
  // For bitfield members
  u32 mBitOffset{ 0 };
  u16 mBitSize{ 0 };
  // For text: code unit size in bytes
  u8 mWidth{ 1 };
  // For TextConst
  std::string mText;
  SourceLoc mLoc;
};

struct BoundTemplatePart
{
  std::string_view mLiteral;
  FormatSpec mFormat{ FormatSpec::Default };
  u32 mFunction{ kNoFunction };
};

struct BoundView
{
  const ViewDefinition *mDef{ nullptr };
  // The type the view's pattern matched. This can be a base class of the type of the displayed value.
  sym::Type *mMatchedType{ nullptr };
  // Offset of the matched (base) type within the displayed value
  u32 mRootOffset{ 0 };
  TypeId mRootType{ kNoType };
  Program mProgram;
  TypeId mThisType{ kNoType };

  std::vector<BoundType> mTypes;
  std::vector<BoundNode> mNodes;
  std::vector<TypeId> mRootTypes;
  std::vector<u32> mLetFunctions;
  std::vector<NodeId> mCallArgs;

  SummarySpec::Kind mSummaryKind{ SummarySpec::Kind::Default };
  std::vector<BoundTemplatePart> mSummaryParts;
  u32 mSummaryFunction{ kNoFunction };
  FormatSpec mSummaryFormat{ FormatSpec::Default };

  ChildrenSpec::Kind mChildrenKind{ ChildrenSpec::Kind::Default };
  u32 mChildrenFunction{ kNoFunction };
  u32 mSizeFunction{ kNoFunction };
  std::optional<u64> mChildrenLimit;

  // If binding the view against the type failed, this is why. The view is then still "the view for this type",
  // but it displays the error instead of a summary.
  std::optional<Diagnostic> mBindError;

  [[nodiscard]] const BoundType &
  Type(TypeId id) const noexcept
  {
    return mTypes[id];
  }

  [[nodiscard]] const BoundNode &
  Node(NodeId id) const noexcept
  {
    return mNodes[id];
  }
};

} // namespace mdb::views
