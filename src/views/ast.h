/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <common/typedefs.h>
#include <views/diagnostic.h>

// std
#include <deque>
#include <limits>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mdb::views {

// Index of an expression in its owning ExprArena::mExprs. Used to link child
// expressions and refer to expressions from statements and call arguments without
// storing pointers that vector growth could invalidate. IDs are local to an arena;
// kNoExpr denotes an absent expression.
using ExprId = u32;
static constexpr ExprId kNoExpr = std::numeric_limits<ExprId>::max();

enum class ExprKind : u8
{
  IntLiteral,
  FloatLiteral,
  CharLiteral,
  StringLiteral,
  BoolLiteral,
  NullLiteral,
  This,
  Identifier,
  // a.b
  Member,
  // a->b
  Arrow,
  // a[b]
  Index,
  // name(args...)
  Call,
  // view(a).name(args...): mA is the object, mName the function
  ViewCall,
  // view(a).summary
  ViewSummary,
  Unary,
  Binary,
  // a ? b : c
  Ternary,
  // (T)a
  Cast,
  SizeofType,
  SizeofExpr,
};

enum class UnaryOp : u8
{
  Plus,
  Negate,
  LogicalNot,
  BitNot,
  Deref,
  AddressOf,
};

enum class BinaryOp : u8
{
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
};

std::string_view ToString(BinaryOp op) noexcept;
std::string_view ToString(UnaryOp op) noexcept;

// A type, as written in a cast or sizeof: `unsigned int`, `T*`, `mozilla::Foo**`
struct TypeExpr
{
  // For builtin types these are the keywords (`unsigned`, `long`), otherwise the qualified name parts.
  std::vector<std::string_view> mNameParts;
  bool mIsBuiltin{ false };
  bool mIsRooted{ false };
  u8 mPointerDepth{ 0 };
  SourceLoc mLoc;

  [[nodiscard]] std::string Spelling() const;
  // Name without pointers, e.g. `unsigned int` or `mozilla::Foo`
  [[nodiscard]] std::string BaseSpelling() const;
};

struct CallArg
{
  // Empty for positional arguments, otherwise the name in `name: expr`
  std::string_view mName;
  ExprId mExpr{ kNoExpr };
};

struct alignas(64) Expr
{
  ExprKind mKind;
  u8 mOp{ 0 };
  u16 mString{ 0 };
  SourceLoc mLoc;
  union
  {
    u64 mInteger{ 0 };
    double mFloat;
  };
  // Identifier name, member name or function name.
  std::string_view mName;
  ExprId mA{ kNoExpr };
  ExprId mB{ kNoExpr };
  ExprId mC{ kNoExpr };
  // Index into ExprArena::mTypes for casts and sizeof
  u32 mType{ 0 };
  // Range in ExprArena::mArgs for calls
  u32 mFirstArg{ 0 };
  u32 mArgCount{ 0 };
  // Index into ExprArena::mStrings for string literals

  [[nodiscard]] UnaryOp
  Unary() const noexcept
  {
    return static_cast<UnaryOp>(mOp);
  }

  [[nodiscard]] BinaryOp
  Binary() const noexcept
  {
    return static_cast<BinaryOp>(mOp);
  }
};

static_assert(sizeof(Expr) == 64, "Remove alignas(64) if this changes");

using StmtId = u32;
static constexpr StmtId kNoStmt = std::numeric_limits<StmtId>::max();

enum class StmtKind : u8
{
  Block,
  Declare,
  Assign,
  Increment,
  If,
  While,
  For,
  Continue,
  Break,
  Return,
  Yield,
  // A call as a statement: `walk(n);`
  Call,
};

enum class AssignOp : u8
{
  Set,
  Add,
  Sub
}; // =, +=, -=. Increment: Add or Sub

struct Stmt
{
  StmtKind mKind;
  SourceLoc mLoc;
  // Declare: the declared name. Assign/Increment: the assigned name
  std::string_view mName;
  // Declare: the type (index into ExprArena::mTypes), unless `auto`
  std::optional<u32> mType;
  AssignOp mOperator{ AssignOp::Set };
  // Declare/Assign: the value. If/While/For: the condition. Return: the value, or kNoExpr.
  // Yield: the value. Call: the call expression
  ExprId mExpr{ kNoExpr };
  // Yield: the key, or kNoExpr
  ExprId mKey{ kNoExpr };
  // If: then/else. While: body. For: init, step, body
  StmtId mA{ kNoStmt }, mB{ kNoStmt }, mC{ kNoStmt };
  // Block: a range in ExprArena::mBlockItems
  u32 mFirst{ 0 }, mCount{ 0 };
};

struct ExprArena
{
  std::vector<Expr> mExprs;
  std::vector<Stmt> mStmts;
  std::vector<StmtId> mBlockItems;
  std::vector<TypeExpr> mTypes;
  std::vector<CallArg> mArgs;
  // deque: references must stay valid as more strings are added
  std::deque<std::string> mStrings;

  [[nodiscard]] const Expr &
  Get(ExprId id) const noexcept
  {
    return mExprs[id];
  }

  [[nodiscard]] const Stmt &
  GetStmt(StmtId id) const noexcept
  {
    return mStmts[id];
  }

  [[nodiscard]] std::span<const CallArg>
  Args(const Expr &call) const noexcept
  {
    return std::span{ mArgs }.subspan(call.mFirstArg, call.mArgCount);
  }

  [[nodiscard]] std::span<const StmtId>
  BlockItems(const Stmt &block) const noexcept
  {
    return std::span{ mBlockItems }.subspan(block.mFirst, block.mCount);
  }
};

// Which types a view applies to: a regular expression (ECMAScript syntax) that must match the *whole* normalized,
// qualified name of a type, e.g. `nsTArray<.*>` or `mozilla::detail::nsTStringRepr<.*>`. See QualifiedTypeName for
// what names look like.
struct TypePattern
{
  // As written in the view file
  std::string mSource;
  std::regex mRegex;
  SourceLoc mLoc;

  [[nodiscard]] bool
  Matches(std::string_view qualifiedName) const
  {
    return std::regex_match(qualifiedName.begin(), qualifiedName.end(), mRegex);
  }
};

enum class FormatSpec : char
{
  Default = 0,
  Hex = 'x',
  HexUpper = 'X',
  Decimal = 'd',
  Octal = 'o',
  Binary = 'b',
  Character = 'c',
};

struct TemplatePart
{
  // Literal text preceding the expression (escapes already processed).
  std::string mLiteral;
  // kNoExpr for a trailing literal
  ExprId mExpr{ kNoExpr };
  FormatSpec mFormat{ FormatSpec::Default };
};

struct SummarySpec
{
  enum class Kind : u8
  {
    // No summary given: the type name is displayed
    Default,
    // summary "len={mLength}"
    Template,
    // summary text(mData, mLength)
    Expression,
  } mKind{ Kind::Default };
  std::vector<TemplatePart> mParts;
  ExprId mExpr{ kNoExpr };
  FormatSpec mFormat{ FormatSpec::Default };
  SourceLoc mLoc;
};

struct ChildrenSpec
{
  enum class Kind : u8
  {
    // The type's own members
    Default,
    // Not expandable
    None,
    // children array(ptr, count)
    Expression,
    // A children block
    Block,
  } mKind{ Kind::Default };
  ExprId mExpr{ kNoExpr };
  StmtId mBody{ kNoStmt };
  SourceLoc mLoc;
};

struct LetBinding
{
  std::string_view mName;
  ExprId mExpr{ kNoExpr };
  SourceLoc mLoc;
};

struct Parameter
{
  std::string_view mName;
  // Index into ExprArena::mTypes
  u32 mType;
  SourceLoc mLoc;
  // `Type &name`: refers to a local of the caller
  bool mIsReference{ false };
};

// `fn name(params) -> T { ... }`, or without `-> T` for a void function, which can yield
struct FunctionDef
{
  std::string_view mName;
  std::vector<Parameter> mParams;
  // Index into ExprArena::mTypes. None: a void function

  [[nodiscard]] bool
  HasReturnType() const
  {
    return mReturnType.has_value();
  }

  std::optional<u32> mReturnType;
  StmtId mBody{ kNoStmt };
  SourceLoc mLoc;
};

// `using T = ...;`: names a type for use in casts and sizeof.
struct UsingBinding
{
  enum class Kind : u8
  {
    // using Header = nsTArrayHeader
    Type,
    // using T = typeof(*mBegin): the static type of an expression, which is not evaluated
    TypeOf,
    // using T = template_arg(0): the matched type's first template argument
    TemplateArgument,
    // using T = template_arg_of(SomeType, 0)
    TemplateArgumentOf
  } mKind{ Kind::Type };
  std::string_view mOf;
  std::string_view mName;
  // Kind::Type: index into ExprArena::mTypes
  u32 mType{ 0 };
  // Kind::TypeOf
  ExprId mExpr{ kNoExpr };
  // Kind::TemplateArgument
  u32 mIndex{ 0 };
  SourceLoc mLoc;
};

// Lets and usings, in the order they're written: a declaration can only refer to the ones before it.
struct Declaration
{
  enum class Kind : u8
  {
    Let,
    Using,
  } mKind;
  // Index into ViewDefinition::mLets or ViewDefinition::mUsings
  u32 mIndex;
};

// Owns parsed mscript source. AST string_views point into mSource, so this must remain
// heap-allocated and never move (including when mSource uses small-string storage).
struct ParsedSource
{
  std::string mPath;
  std::string mSource;
  ExprArena mArena;
  std::vector<Diagnostic> mDiagnostics;

  ParsedSource() noexcept = default;
  ParsedSource(const ParsedSource &) = delete;
  ParsedSource &operator=(const ParsedSource &) = delete;
  ParsedSource(ParsedSource &&) = delete;
  ParsedSource &operator=(ParsedSource &&) = delete;
};

struct ViewDefinition
{
  // The pattern this view matches against
  TypePattern mPattern;
  // The object file, which this view is applied in
  std::optional<std::string> mObjectFile;
  std::vector<LetBinding> mLets;
  std::vector<UsingBinding> mUsings;
  std::vector<FunctionDef> mFunctions;
  std::vector<Declaration> mDeclarations;
  SummarySpec mSummary;
  ChildrenSpec mChildren;
  SourceLoc mLoc;
  const ParsedSource *mSource{ nullptr };
};

struct ViewFile
{
  ParsedSource mUnit;
  std::vector<ViewDefinition> mViews;
};

// A template bound against a stack frame. Valid only when mUnit.mDiagnostics is empty.
struct FrameTemplate
{
  ParsedSource mUnit;
  SummarySpec mSummary;
};

} // namespace mdb::views
