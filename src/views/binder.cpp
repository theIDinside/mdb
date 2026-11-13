/** LICENSE TEMPLATE */
#include "binder.h"

// mdb
#include <symbolication/dwarf/die.h>
#include <symbolication/dwarf/name_index.h>
#include <symbolication/objfile.h>
#include <symbolication/type.h>
#include <views/builtin_types.h>
#include <views/codegen.h>
#include <views/parser.h>

// std
#include <algorithm>
#include <cassert>
#include <cstring>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace mdb::views {

namespace {

struct BoundExpr
{
  NodeId mNode{ kNoNode };
  bool mIsLValue{ false };
};

using BindResult = std::optional<BoundExpr>;

struct ScopeEntry
{
  enum class Kind : u8
  {
    Let,
    Local,
    Type
  } mKind;
  bool mIsLValue{ false };
  bool mIsReference{ false };
  u32 mIndex{ 0 };
  TypeId mType{ kNoType };
};

using Scope = std::unordered_map<std::string_view, ScopeEntry>;

class Binder;

class BindingEnvironment
{
public:
  virtual ~BindingEnvironment() noexcept = default;
  virtual void PopulateGlobalScope(Binder &binder, Scope &scope) noexcept = 0;
  virtual BindResult ResolveUnknownName(Binder &binder, const Expr &expr) noexcept = 0;
  virtual BindResult ResolveThis(Binder &binder, const Expr &expr) noexcept = 0;
};

class FrameEnvironment final : public BindingEnvironment
{
  std::span<const FrameVariable> mVariables;
  std::unordered_map<u32, u32> mRootOf;
  std::optional<u32> mThisVariable;

public:
  std::vector<u32> mRootVariables;
  explicit FrameEnvironment(std::span<const FrameVariable> variables) noexcept : mVariables(variables)
  {
    for (u32 i = 0; i < variables.size(); ++i) {
      if (variables[i].mName == "this") {
        mThisVariable = i;
        break;
      }
    }
  }
  void
  PopulateGlobalScope(Binder &, Scope &) noexcept override
  {
  }
  BindResult ResolveUnknownName(Binder &, const Expr &) noexcept override;
  BindResult ResolveThis(Binder &, const Expr &) noexcept override;
  BoundExpr VariableExpr(Binder &, u32 variable, SourceLoc loc) noexcept;
  u32 RootFor(Binder &, u32) noexcept;
};

class ViewEnvironment;

struct FieldLookup
{
  const sym::Field *mField;
  u32 mOffset;
};

class Binder
{
  friend class ViewEnvironment;
  friend class FrameEnvironment;

  const ParsedSource &mUnit;
  const ViewDefinition *mDef;
  SourceLoc mCodeLoc;
  const ExprArena &mArena;
  CodeObject &mCodeObject;
  sym::Type *mMatchedType;
  ObjectFile *mObjectFile;
  BindingEnvironment &mEnvironment;
  const ViewLookup &mViewLookup;
  CodeGen mCodeGen;
  std::vector<Scope> mScopes;
  struct LoopTargets
  {
    std::vector<u32> mBreaks;
    std::vector<u32> mContinues;
  };
  std::vector<LoopTargets> mLoops;
  u32 mCurrentFunction{ kNoFunction };

  std::unordered_map<sym::Type *, TypeId> mDwarfTypes;
  std::unordered_map<std::string_view, TypeId> mBuiltinTypes;
  // For builtin types: how to find them in the debug info, if a DWARF type is needed (see EnsureDwarfType)
  std::unordered_map<TypeId, BuiltinType> mBuiltinDefinitions;
  std::unordered_map<TypeId, TypeId> mPointerTypes;
  std::unordered_set<TypeId> mReferenceTypes;
  std::vector<bool> mLetIsLValue;
  std::vector<NodeId> mLetNodes;
  std::vector<NodeId> mSummaryPartNodes;
  std::unordered_map<std::string_view, u32> mFunctionIndex;
  std::vector<u8> mSignatureState;
  std::vector<std::vector<bool>> mFunctionReferences;
  NodeId mSummaryNode{ kNoNode };
  NodeId mChildrenNode{ kNoNode };
  TypeId mTextType{ kNoType };
  TypeId mI64{ kNoType };
  TypeId mU64{ kNoType };
  TypeId mF64{ kNoType };
  TypeId mBool{ kNoType };

public:
  std::optional<Diagnostic> mError;

  Binder(const ParsedSource &unit,
    const ViewDefinition *def,
    CodeObject &code,
    sym::Type *matchedType,
    ObjectFile *objectFile,
    BindingEnvironment &environment,
    const ViewLookup &viewLookup)
      : mUnit(unit), mDef(def), mArena(unit.mArena), mCodeObject(code), mMatchedType(matchedType),
        mObjectFile(objectFile), mEnvironment(environment), mViewLookup(viewLookup), mCodeGen(code)
  {
    mI64 = FromBuiltin(BuiltinI64());
    mU64 = FromBuiltin(BuiltinU64());
    mF64 = FromBuiltin(BuiltinF64());
    mBool = FromBuiltin(BuiltinBool());
    mTextType = AddType(BoundType{ .mKind = TypeKind::Text, .mName = "text" });
  }

  bool BindAll() noexcept;
  bool BindFrame(const SummarySpec &summary) noexcept;
  bool BindTemplate(const SummarySpec &summary) noexcept;
  void EmitCode() noexcept;

private:
  bool
  Fail(SourceLoc loc, std::string message) noexcept
  {
    if (!mError) {
      mError = Diagnostic{ .mFile = mUnit.mPath, .mLoc = loc, .mMessage = std::move(message) };
    }
    return false;
  }

  BindResult
  FailExpr(SourceLoc loc, std::string message) noexcept
  {
    Fail(loc, std::move(message));
    return std::nullopt;
  }

  TypeId
  AddType(BoundType type) noexcept
  {
    mCodeObject.mTypes.push_back(std::move(type));
    return static_cast<TypeId>(mCodeObject.mTypes.size() - 1);
  }

  [[nodiscard]] const BoundType &
  Type(TypeId id) const noexcept
  {
    return mCodeObject.mTypes[id];
  }

  [[nodiscard]] TypeId
  TypeOf(NodeId node) const noexcept
  {
    return mCodeObject.mNodes[node].mType;
  }

  [[nodiscard]] const BoundType &
  TypeOf(const BoundExpr &expr) const noexcept
  {
    return Type(TypeOf(expr.mNode));
  }

  NodeId
  AddNode(BoundNode node) noexcept
  {
    mCodeObject.mNodes.push_back(std::move(node));
    return static_cast<NodeId>(mCodeObject.mNodes.size() - 1);
  }

  NodeId
  AddNode(BoundOp op, TypeId type, SourceLoc loc, NodeId a = kNoNode, NodeId b = kNoNode, NodeId c = kNoNode)
  {
    return AddNode(BoundNode{ .mOp = op, .mType = type, .mA = a, .mB = b, .mC = c, .mLoc = loc });
  }

  TypeId FromDwarf(sym::Type *type) noexcept;
  const sym::TemplateParameter *TemplateArgumentOf(sym::Type *type, u64 index, SourceLoc loc) noexcept;
  std::optional<TypeId> BindUsing(const UsingBinding &binding) noexcept;
  TypeId FromBuiltin(const BuiltinType &builtin) noexcept;
  TypeId PointerTo(TypeId element) noexcept;
  std::optional<TypeId> ResolveTypeExpr(const TypeExpr &type) noexcept;
  sym::Type *LookupNamedType(const TypeExpr &type) noexcept;
  sym::Type *LookupDwarfBaseType(const BuiltinType &builtin) noexcept;
  sym::Type *EnsureDwarfType(TypeId type) noexcept;

  std::optional<FieldLookup> FindField(sym::Type *record, std::string_view name) noexcept;
  BindResult MemberOf(const BoundExpr &base, std::string_view name, SourceLoc loc) noexcept;
  BindResult AutoDeref(const BoundExpr &expr, SourceLoc loc) noexcept;

  std::optional<NodeId> RValue(const BoundExpr &expr, SourceLoc loc) noexcept;
  std::optional<NodeId> ScalarRValue(ExprId expr, std::string_view context) noexcept;
  std::optional<NodeId> ScalarRValueOf(const BoundExpr &bound, SourceLoc loc, std::string_view context) noexcept;
  TypeId ImportType(const CodeObject &from, TypeId id) noexcept;
  BindResult BindViewCall(const Expr &expr) noexcept;
  std::optional<NodeId> IntegerRValue(ExprId expr, std::string_view context) noexcept;
  NodeId ConvertTo(NodeId node, TypeId type, SourceLoc loc) noexcept;
  NodeId ToBool(NodeId node, SourceLoc loc) noexcept;
  TypeId CommonArithmeticType(TypeId a, TypeId b) noexcept;
  std::optional<u64> ConstantArgument(const CallArg &arg) noexcept;

  BindResult Bind(ExprId id) noexcept;
  BindResult BindIdentifier(const Expr &expr) noexcept;
  [[nodiscard]] const ScopeEntry *LookupName(std::string_view name) const noexcept;
  BindResult BindUnary(const Expr &expr) noexcept;
  BindResult BindBinary(const Expr &expr) noexcept;
  BindResult CombineBinary(BinaryOp op, NodeId lhs, NodeId rhs, SourceLoc loc) noexcept;
  BindResult BindTernary(const Expr &expr) noexcept;
  BindResult BindCast(const Expr &expr) noexcept;
  BindResult BindCall(const Expr &expr) noexcept;
  bool EnsureSignature(u32 function) noexcept;
  std::optional<TypeId> ResolveReturnType(const TypeExpr &type) noexcept;
  std::optional<std::vector<NodeId>> BindFunctionArguments(const Expr &expr, u32 function) noexcept;
  NodeId AddCallNode(const Expr &expr, u32 function, std::span<const NodeId> args, TypeId returnType) noexcept;
  bool LowerCallStatement(ExprId exprId, SourceLoc loc) noexcept;
  BindResult BindPointerArithmetic(BinaryOp op, NodeId lhs, NodeId rhs, SourceLoc loc) noexcept;
  bool LowerStatement(StmtId id) noexcept;
  bool LowerDeclaration(const Stmt &stmt) noexcept;
  bool LowerAssignment(const Stmt &stmt) noexcept;
};

class ViewEnvironment final : public BindingEnvironment
{
  const ViewDefinition &mDef;

public:
  explicit ViewEnvironment(const ViewDefinition &def) noexcept : mDef(def) {}

  void
  PopulateGlobalScope(Binder &binder, Scope &scope) noexcept override
  {
    static constexpr std::string_view builtins[] = {
      "template_arg", "typeof", "array", "text", "cstr", "utf8", "utf16", "utf32", "sizeof", "view"
    };
    binder.mSignatureState.resize(mDef.mFunctions.size());
    binder.mFunctionReferences.resize(mDef.mFunctions.size());
    for (const auto &function : mDef.mFunctions) {
      if (std::ranges::find(builtins, function.mName) != std::end(builtins)) {
        binder.Fail(
          function.mLoc, std::format("'{}' is a builtin and can't be used as a function name", function.mName));
        return;
      }
      const u32 index = binder.mCodeGen.AddFunction(std::string{ function.mName }, !function.mReturnType);
      binder.mFunctionIndex.emplace(function.mName, index);
    }

    for (const auto &declaration : mDef.mDeclarations) {
      if (declaration.mKind == Declaration::Kind::Using) {
        const auto &binding = mDef.mUsings[declaration.mIndex];
        auto type = binder.BindUsing(binding);
        if (!type) {
          return;
        }
        scope.emplace(binding.mName, ScopeEntry{ .mKind = ScopeEntry::Kind::Type, .mType = *type });
        continue;
      }

      const auto &binding = mDef.mLets[declaration.mIndex];
      auto bound = binder.Bind(binding.mExpr);
      if (!bound) {
        return;
      }
      binder.mLetNodes.push_back(bound->mNode);
      binder.mLetIsLValue.push_back(bound->mIsLValue);
      scope.emplace(binding.mName,
        ScopeEntry{ .mKind = ScopeEntry::Kind::Let, .mIsLValue = bound->mIsLValue, .mIndex = declaration.mIndex });
    }
  }

  BindResult
  ResolveUnknownName(Binder &binder, const Expr &expr) noexcept override
  {
    auto self = ResolveThis(binder, expr);
    return binder.MemberOf(*self, expr.mName, expr.mLoc);
  }

  // In a view, 'this' is the viewed object.
  BindResult
  ResolveThis(Binder &binder, const Expr &expr) noexcept override
  {
    return BoundExpr{ .mNode = binder.AddNode(BoundOp::This, binder.mCodeObject.mRootType, expr.mLoc),
      .mIsLValue = true };
  }
};

TypeId
Binder::FromBuiltin(const BuiltinType &builtin) noexcept
{
  if (auto it = mBuiltinTypes.find(builtin.mName); it != mBuiltinTypes.end()) {
    return it->second;
  }
  BoundType type{ .mKind = builtin.mKind,
    .mSize = builtin.mSize,
    .mIsChar = builtin.mIsChar,
    .mIsSigned = builtin.mKind == TypeKind::SignedInt,
    .mName = std::string{ builtin.mName } };
  const TypeId id = AddType(std::move(type));
  mBuiltinTypes.emplace(builtin.mName, id);
  mBuiltinDefinitions.emplace(id, builtin);
  return id;
}

sym::Type *
Binder::EnsureDwarfType(TypeId id) noexcept
{
  if (mCodeObject.mTypes[id].mDwarf == nullptr) {
    if (auto it = mBuiltinDefinitions.find(id); it != mBuiltinDefinitions.end()) {
      mCodeObject.mTypes[id].mDwarf = LookupDwarfBaseType(it->second);
    }
  }
  return mCodeObject.mTypes[id].mDwarf;
}

const ScopeEntry *
Binder::LookupName(std::string_view name) const noexcept
{
  for (auto scope = mScopes.rbegin(); scope != mScopes.rend(); ++scope) {
    if (const auto entry = scope->find(name); entry != scope->end()) {
      return &entry->second;
    }
  }
  return nullptr;
}

sym::Type *
Binder::LookupDwarfBaseType(const BuiltinType &builtin) noexcept
{
  if (mObjectFile == nullptr || builtin.mKind == TypeKind::Void) {
    return nullptr;
  }
  auto *index = mObjectFile->GetNameIndex();
  if (index == nullptr) {
    return nullptr;
  }
  sym::Type *found = nullptr;
  for (const auto name : builtin.mDwarfNames) {
    if (name.empty() || found) {
      break;
    }
    index->ForEachType(name, [&](const sym::dw::DieNameReference &ref) {
      if (found || !ref.IsValid()) {
        return;
      }
      auto die = ref.cu->GetDieByCacheIndex(ref.die_index);
      if (die.GetDie() == nullptr || die.GetDie()->mTag != DwarfTag::DW_TAG_base_type) {
        return;
      }
      sym::Type *type = mObjectFile->GetTypeStorage()->GetOrCreateNewType(die.AsIndexed());
      if (type && type->Size() == builtin.mSize) {
        found = sym::CanonicalType::From(*type).TypeEntry();
      }
    });
  }
  return found;
}

TypeId
Binder::PointerTo(TypeId element) noexcept
{
  if (auto it = mPointerTypes.find(element); it != mPointerTypes.end()) {
    return it->second;
  }
  const TypeId id = AddType(BoundType{ .mKind = TypeKind::Pointer,
    .mSize = 8,
    .mElement = element,
    .mName = std::format("{} *", Type(element).mName) });
  mPointerTypes.emplace(element, id);
  return id;
}

TypeId
Binder::FromDwarf(sym::Type *raw) noexcept
{
  if (raw == nullptr) {
    return FromBuiltin(LookupBuiltinType(std::array{ std::string_view{ "void" } }).value());
  }
  sym::Type *type = sym::CanonicalType::From(*raw).TypeEntry();
  if (auto it = mDwarfTypes.find(type); it != mDwarfTypes.end()) {
    return it->second;
  }

  // Register before recursing into pointee/element types.
  const TypeId id = AddType(BoundType{ .mDwarf = type, .mName = std::format("{}", *type) });
  mDwarfTypes.emplace(type, id);

  BoundType result{ .mSize = type->Size(), .mDwarf = type, .mName = mCodeObject.mTypes[id].mName };
  const auto modifier = *type->mModifier;
  if (modifier == sym::Modifier::Pointer || modifier == sym::Modifier::Reference ||
      modifier == sym::Modifier::RValueReference) {
    result.mKind = TypeKind::Pointer;
    result.mSize = 8;
    result.mElement = FromDwarf(type->TypeChain());
    if (modifier != sym::Modifier::Pointer) {
      mReferenceTypes.insert(id);
    }
  } else if (modifier == sym::Modifier::Array) {
    result.mKind = TypeKind::Array;
    result.mElement = FromDwarf(type->TypeChain());
    result.mCount = type->ArraySize();
    result.mSize = result.mCount * Type(result.mElement).mSize;
  } else if (type->GetDwarfTag() == DwarfTag::DW_TAG_enumeration_type) {
    result.mKind = TypeKind::Enum;
    result.mIsSigned = type->GetEnumerations().mIsSigned;
  } else if (const auto encoding = type->GetBaseType(); encoding) {
    switch (*encoding) {
    case BaseTypeEncoding::DW_ATE_boolean:
      result.mKind = TypeKind::Bool;
      break;
    case BaseTypeEncoding::DW_ATE_float:
      result.mKind = TypeKind::Float;
      break;
    case BaseTypeEncoding::DW_ATE_signed_char:
      result.mIsChar = true;
      [[fallthrough]];
    case BaseTypeEncoding::DW_ATE_signed:
      result.mKind = TypeKind::SignedInt;
      result.mIsSigned = true;
      // `wchar_t` is a DW_ATE_signed in gcc's output
      result.mIsChar = result.mIsChar || type->mName == "wchar_t";
      break;
    case BaseTypeEncoding::DW_ATE_unsigned_char:
    case BaseTypeEncoding::DW_ATE_UTF:
      result.mIsChar = true;
      [[fallthrough]];
    case BaseTypeEncoding::DW_ATE_unsigned:
      result.mKind = TypeKind::UnsignedInt;
      break;
    default:
      // Unsupported encodings (complex, decimal float, ...) can't be computed with.
      result.mKind = TypeKind::Record;
      break;
    }
  } else if (type->GetDwarfTag() == DwarfTag::DW_TAG_structure_type ||
             type->GetDwarfTag() == DwarfTag::DW_TAG_class_type ||
             type->GetDwarfTag() == DwarfTag::DW_TAG_union_type) {
    result.mKind = TypeKind::Record;
  } else {
    // void (the unit type), function types
    result.mKind = TypeKind::Void;
    result.mSize = 0;
  }

  mCodeObject.mTypes[id] = std::move(result);
  return id;
}

sym::Type *
Binder::LookupNamedType(const TypeExpr &typeExpr) noexcept
{
  if (mObjectFile == nullptr) {
    return nullptr;
  }
  auto *index = mObjectFile->GetNameIndex();
  if (index == nullptr) {
    return nullptr;
  }
  // `ns::Foo` matches a type named exactly that, or one nested deeper (`outer::ns::Foo`), unless written
  // `::ns::Foo`.
  std::string written;
  for (const auto part : typeExpr.mNameParts) {
    written.append(written.empty() ? "" : "::");
    written.append(part);
  }
  const std::string nested = "::" + written;
  sym::Type *found = nullptr;
  index->ForEachType(typeExpr.mNameParts.back(), [&](const sym::dw::DieNameReference &ref) {
    if (found || !ref.IsValid()) {
      return;
    }
    auto die = ref.cu->GetDieByCacheIndex(ref.die_index);
    if (die.GetDie() == nullptr) {
      return;
    }
    sym::Type *type = mObjectFile->GetTypeStorage()->GetOrCreateNewType(die.AsIndexed());
    if (type == nullptr) {
      return;
    }
    sym::Type *canonical = sym::CanonicalType::From(*type).TypeEntry();
    const std::string name = QualifiedTypeName(canonical);
    if (name == written || (!typeExpr.mIsRooted && name.ends_with(nested))) {
      found = canonical;
    }
  });
  return found;
}

std::optional<TypeId>
Binder::ResolveTypeExpr(const TypeExpr &typeExpr) noexcept
{
  std::optional<TypeId> base;
  if (typeExpr.mIsBuiltin) {
    const auto builtin = LookupBuiltinType(typeExpr.mNameParts);
    if (!builtin) {
      Fail(typeExpr.mLoc, std::format("'{}' is not a valid type", typeExpr.BaseSpelling()));
      return std::nullopt;
    }
    base = FromBuiltin(*builtin);
  } else if (typeExpr.mNameParts.size() == 1 && !typeExpr.mIsRooted) {
    if (const ScopeEntry *entry = LookupName(typeExpr.mNameParts[0]);
      entry != nullptr && entry->mKind == ScopeEntry::Kind::Type) {
      base = entry->mType;
    }
  }

  if (!base) {
    sym::Type *named = LookupNamedType(typeExpr);
    if (named == nullptr) {
      Fail(typeExpr.mLoc, std::format("unknown type '{}'", typeExpr.BaseSpelling()));
      return std::nullopt;
    }
    base = FromDwarf(named);
  }

  TypeId result = *base;
  for (u8 i = 0; i < typeExpr.mPointerDepth; ++i) {
    result = PointerTo(result);
  }
  return result;
}

bool
Binder::EnsureSignature(u32 function) noexcept
{
  assert(mDef);
  if (mSignatureState[function] == 2) {
    return true;
  }
  if (mSignatureState[function] == 1) {
    return true;
  }
  mSignatureState[function] = 1;
  const FunctionDef &definition = mDef->mFunctions[function];
  for (const Parameter &parameter : definition.mParams) {
    auto type = ResolveTypeExpr(mArena.mTypes[parameter.mType]);
    if (!type) {
      return false;
    }
    if (!Type(*type).IsScalar()) {
      return Fail(parameter.mLoc,
        std::format("parameter '{}' of '{}' must be a number or a pointer", parameter.mName, definition.mName));
    }
    mFunctionReferences[function].push_back(parameter.mIsReference);
    mCodeGen.AddLocal(function, *type);
  }

  TypeId returnType = kNoType;
  if (definition.mReturnType) {
    auto type = ResolveReturnType(mArena.mTypes[*definition.mReturnType]);
    if (!type) {
      return false;
    }
    if (!Type(*type).IsScalar() && Type(*type).mKind != TypeKind::Text) {
      return Fail(mArena.mTypes[*definition.mReturnType].mLoc,
        std::format("return type of '{}' must be a number, a pointer or text", definition.mName));
    }
    returnType = *type;
  }
  auto &boundFunction = mCodeObject.mProgram.mFunctions[function];
  boundFunction.mParamCount = static_cast<u32>(definition.mParams.size() + 1);
  boundFunction.mReturn = returnType;
  mSignatureState[function] = 2;
  return true;
}

// `text` is a return type, not a type of the program.
std::optional<TypeId>
Binder::ResolveReturnType(const TypeExpr &type) noexcept
{
  if (!type.mIsBuiltin && !type.mIsRooted && type.mPointerDepth == 0 && type.mNameParts.size() == 1 &&
      type.mNameParts[0] == "text") {
    return mTextType;
  }
  return ResolveTypeExpr(type);
}

std::optional<std::vector<NodeId>>
Binder::BindFunctionArguments(const Expr &expr, u32 function) noexcept
{
  if (!EnsureSignature(function)) {
    return std::nullopt;
  }
  const FunctionDef &definition = mDef->mFunctions[function];
  const auto args = mArena.Args(expr);
  for (const CallArg &arg : args) {
    if (!arg.mName.empty()) {
      Fail(mArena.Get(arg.mExpr).mLoc, std::format("'{}' takes positional arguments", definition.mName));
      return std::nullopt;
    }
  }
  if (args.size() != definition.mParams.size()) {
    const size_t count = definition.mParams.size();
    Fail(expr.mLoc,
      std::format(
        "'{}' takes {} argument{}, got {}", definition.mName, count, count == 1 ? "" : "s", args.size()));
    return std::nullopt;
  }

  std::vector<NodeId> bound;
  bound.reserve(args.size());
  for (size_t i = 0; i < args.size(); ++i) {
    if (mFunctionReferences[function][i]) {
      const Parameter &parameter = definition.mParams[i];
      const Expr &argument = mArena.Get(args[i].mExpr);
      if (argument.mKind != ExprKind::Identifier) {
        Fail(argument.mLoc, std::format("'{}' is a reference parameter, pass a local", parameter.mName));
        return std::nullopt;
      }
      const ScopeEntry *entry = LookupName(argument.mName);
      if (entry == nullptr || entry->mKind != ScopeEntry::Kind::Local) {
        Fail(argument.mLoc, std::format("'{}' is a reference parameter, pass a local", parameter.mName));
        return std::nullopt;
      }
      const TypeId expectedType = mCodeObject.mProgram.mFunctions[function].mLocalTypes[i + 1];
      if (entry->mType != expectedType) {
        Fail(argument.mLoc,
          std::format("'{}' needs a local of type '{}', got '{}'",
            parameter.mName,
            Type(expectedType).mName,
            Type(entry->mType).mName));
        return std::nullopt;
      }
      const BoundOp op = entry->mIsReference ? BoundOp::Local : BoundOp::LocalRef;
      bound.push_back(
        AddNode(BoundNode{ .mOp = op, .mType = entry->mType, .mImm = entry->mIndex, .mLoc = argument.mLoc }));
      continue;
    }
    auto value = ScalarRValue(args[i].mExpr, "a function argument");
    if (!value) {
      return std::nullopt;
    }
    const TypeId parameterType = mCodeObject.mProgram.mFunctions[function].mLocalTypes[i + 1];
    bound.push_back(ConvertTo(*value, parameterType, mArena.Get(args[i].mExpr).mLoc));
  }
  return bound;
}

NodeId
Binder::AddCallNode(const Expr &expr, u32 function, std::span<const NodeId> args, TypeId returnType) noexcept
{
  const u32 first = static_cast<u32>(mCodeObject.mCallArgs.size());
  mCodeObject.mCallArgs.insert(mCodeObject.mCallArgs.end(), args.begin(), args.end());
  return AddNode(BoundNode{ .mOp = BoundOp::Call,
    .mType = returnType,
    .mA = first,
    .mB = static_cast<u32>(args.size()),
    .mImm = function,
    .mLoc = expr.mLoc });
}

std::optional<FieldLookup>
Binder::FindField(sym::Type *record, std::string_view name) noexcept
{
  record->ResolveType();
  const auto fields = record->MemberFields();
  for (const auto &field : fields) {
    if (field.mName == name) {
      return FieldLookup{ .mField = &field, .mOffset = *field.mObjectBaseOffset };
    }
  }
  // Members of anonymous structs and unions are accessed as if they were members of the enclosing type.
  for (const auto &field : fields) {
    const std::string_view fieldName = field.mName;
    if (fieldName != "structure" && fieldName != "union" && fieldName != "class") {
      continue;
    }
    sym::Type *anonymous = sym::CanonicalType::From(field.mType).TypeEntry();
    const std::string_view anonymousName = anonymous->mName;
    if (!anonymousName.empty() && anonymousName != fieldName) {
      // A real member that happens to be named `union`? Not possible in C++, but don't recurse into named types.
      continue;
    }
    if (auto inner = FindField(anonymous, name); inner) {
      inner->mOffset += *field.mObjectBaseOffset;
      return inner;
    }
  }
  return std::nullopt;
}

BindResult
Binder::AutoDeref(const BoundExpr &expr, SourceLoc loc) noexcept
{
  const TypeId type = TypeOf(expr.mNode);
  if (!expr.mIsLValue || !mReferenceTypes.contains(type)) {
    return expr;
  }
  const NodeId pointer = AddNode(BoundOp::Load, type, loc, expr.mNode);
  const NodeId deref = AddNode(BoundOp::Deref, Type(type).mElement, loc, pointer);
  return BoundExpr{ .mNode = deref, .mIsLValue = true };
}

BindResult
Binder::MemberOf(const BoundExpr &base, std::string_view name, SourceLoc loc) noexcept
{
  const BoundType &type = TypeOf(base);
  if (type.mKind != TypeKind::Record || type.mDwarf == nullptr) {
    return FailExpr(
      loc, std::format("'{}' is not a struct, class or union, it has no member '{}'", type.mName, name));
  }
  const auto field = FindField(type.mDwarf, name);
  if (!field) {
    return FailExpr(loc, std::format("no member named '{}' in '{}'", name, type.mName));
  }
  BoundNode node{ .mOp = BoundOp::Member,
    .mType = FromDwarf(field->mField->mType.ptr),
    .mA = base.mNode,
    .mImm = field->mOffset,
    .mLoc = loc };
  if (*field->mField->mBitFieldSize > 0) {
    node.mBitOffset = *field->mField->mFieldOffset;
    node.mBitSize = *field->mField->mBitFieldSize;
  }
  return AutoDeref(BoundExpr{ .mNode = AddNode(std::move(node)), .mIsLValue = true }, loc);
}

std::optional<NodeId>
Binder::RValue(const BoundExpr &expr, SourceLoc loc) noexcept
{
  if (!expr.mIsLValue) {
    return expr.mNode;
  }
  const TypeId typeId = TypeOf(expr.mNode);
  const BoundType &type = Type(typeId);
  switch (type.mKind) {
  case TypeKind::Array: {
    const TypeId pointer = PointerTo(type.mElement);
    return AddNode(BoundOp::Decay, pointer, loc, expr.mNode);
  }
  case TypeKind::Record:
    Fail(loc, std::format("a value of type '{}' can't be used here, use one of its members", type.mName));
    return std::nullopt;
  case TypeKind::Void:
    Fail(loc, std::format("a value of type '{}' can't be used here", type.mName));
    return std::nullopt;
  default:
    return AddNode(BoundOp::Load, typeId, loc, expr.mNode);
  }
}

std::optional<NodeId>
Binder::ScalarRValue(ExprId id, std::string_view context) noexcept
{
  auto bound = Bind(id);
  if (!bound) {
    return std::nullopt;
  }
  return ScalarRValueOf(*bound, mArena.Get(id).mLoc, context);
}

std::optional<NodeId>
Binder::ScalarRValueOf(const BoundExpr &bound, SourceLoc loc, std::string_view context) noexcept
{
  auto value = RValue(bound, loc);
  if (!value) {
    return std::nullopt;
  }
  if (!Type(TypeOf(*value)).IsScalar()) {
    Fail(loc, std::format("{} must be a number or a pointer, not '{}'", context, Type(TypeOf(*value)).mName));
    return std::nullopt;
  }
  return value;
}

std::optional<NodeId>
Binder::IntegerRValue(ExprId id, std::string_view context) noexcept
{
  auto value = ScalarRValue(id, context);
  if (!value) {
    return std::nullopt;
  }
  if (!Type(TypeOf(*value)).IsInteger()) {
    Fail(mArena.Get(id).mLoc, std::format("{} must be an integer, not '{}'", context, Type(TypeOf(*value)).mName));
    return std::nullopt;
  }
  return ConvertTo(*value, mI64, mArena.Get(id).mLoc);
}

NodeId
Binder::ConvertTo(NodeId node, TypeId type, SourceLoc loc) noexcept
{
  if (TypeOf(node) == type) {
    return node;
  }
  return AddNode(BoundOp::Convert, type, loc, node);
}

NodeId
Binder::ToBool(NodeId node, SourceLoc loc) noexcept
{
  return ConvertTo(node, mBool, loc);
}

TypeId
Binder::CommonArithmeticType(TypeId a, TypeId b) noexcept
{
  const BoundType &ta = Type(a);
  const BoundType &tb = Type(b);
  if (ta.mKind == TypeKind::Float || tb.mKind == TypeKind::Float) {
    return mF64;
  }
  // Everything is computed in 64 bits. Only 64-bit unsigned operands make the computation unsigned, which keeps
  // e.g. `u32 - int` from wrapping around the way it would in C.
  const bool unsignedWide = (!ta.mIsSigned && ta.mSize == 8) || (!tb.mIsSigned && tb.mSize == 8);
  return unsignedWide ? mU64 : mI64;
}

std::optional<u64>
Binder::ConstantArgument(const CallArg &arg) noexcept
{
  const Expr &expr = mArena.Get(arg.mExpr);
  if (expr.mKind != ExprKind::IntLiteral) {
    Fail(expr.mLoc, std::format("'{}' must be an integer literal", arg.mName));
    return std::nullopt;
  }
  return expr.mInteger;
}

BindResult
Binder::BindIdentifier(const Expr &expr) noexcept
{
  const std::string_view name = expr.mName;
  if (const ScopeEntry *entry = LookupName(name); entry != nullptr) {
    switch (entry->mKind) {
    case ScopeEntry::Kind::Let: {
      const NodeId node = AddNode(BoundNode{ .mOp = BoundOp::LetRef,
        .mType = TypeOf(mLetNodes[entry->mIndex]),
        .mImm = static_cast<u64>(entry->mIndex),
        .mLoc = expr.mLoc });
      return BoundExpr{ .mNode = node, .mIsLValue = mLetIsLValue[entry->mIndex] };
    }
    case ScopeEntry::Kind::Local: {
      const BoundOp op = entry->mIsReference ? BoundOp::LoadRef : BoundOp::Local;
      const NodeId node =
        AddNode(BoundNode{ .mOp = op, .mType = entry->mType, .mImm = entry->mIndex, .mLoc = expr.mLoc });
      return BoundExpr{ .mNode = node, .mIsLValue = false };
    }
    case ScopeEntry::Kind::Type:
      return FailExpr(expr.mLoc, std::format("'{}' is a type; use it in a cast or sizeof", name));
    }
  }
  return mEnvironment.ResolveUnknownName(*this, expr);
}

BindResult
Binder::BindPointerArithmetic(BinaryOp op, NodeId lhs, NodeId rhs, SourceLoc loc) noexcept
{
  const BoundType &lt = Type(TypeOf(lhs));
  const BoundType &rt = Type(TypeOf(rhs));
  const bool lptr = lt.mKind == TypeKind::Pointer;
  const bool rptr = rt.mKind == TypeKind::Pointer;

  const auto elementSize = [this](const BoundType &pointer) -> u64 {
    const u32 size = Type(pointer.mElement).mSize;
    // Arithmetic on void* (and other sizeless pointees) is byte based, like in GNU C.
    return size == 0 ? 1 : size;
  };

  if (lptr && rptr) {
    if (op != BinaryOp::Sub) {
      return FailExpr(loc, std::format("invalid operator '{}' between two pointers", ToString(op)));
    }
    if (elementSize(lt) != elementSize(rt)) {
      return FailExpr(loc, std::format("can't subtract pointers of type '{}' and '{}'", lt.mName, rt.mName));
    }
    NodeId node = AddNode(BoundOp::PointerDiff, mI64, loc, lhs, rhs);
    mCodeObject.mNodes[node].mImm = elementSize(lt);
    return BoundExpr{ .mNode = node };
  }

  if (op != BinaryOp::Add && op != BinaryOp::Sub) {
    return FailExpr(loc, std::format("invalid operator '{}' on a pointer", ToString(op)));
  }
  NodeId pointer = lptr ? lhs : rhs;
  NodeId offset = lptr ? rhs : lhs;
  if (!lptr && op == BinaryOp::Sub) {
    return FailExpr(loc, "can't subtract a pointer from an integer");
  }
  if (!Type(TypeOf(offset)).IsInteger()) {
    return FailExpr(loc, "pointer arithmetic needs an integer offset");
  }
  offset = ConvertTo(offset, mI64, loc);
  const BoundType &pointerType = Type(TypeOf(pointer));
  const u64 scale = elementSize(pointerType);
  NodeId node = AddNode(
    op == BinaryOp::Add ? BoundOp::PointerAdd : BoundOp::PointerSub, TypeOf(pointer), loc, pointer, offset);
  mCodeObject.mNodes[node].mImm = scale;
  return BoundExpr{ .mNode = node };
}

BindResult
Binder::BindBinary(const Expr &expr) noexcept
{
  const auto op = expr.Binary();
  auto left = Bind(expr.mA);
  if (!left) {
    return std::nullopt;
  }
  if (op == BinaryOp::Add && TypeOf(*left).mKind == TypeKind::Text) {
    auto right = Bind(expr.mB);
    if (!right) {
      return std::nullopt;
    }
    if (TypeOf(*right).mKind != TypeKind::Text) {
      return FailExpr(mArena.Get(expr.mB).mLoc,
        std::format("text can only be joined with text, not '{}'; format numbers in a summary template",
          TypeOf(*right).mName));
    }
    return BoundExpr{ .mNode = AddNode(BoundOp::Concat, mTextType, expr.mLoc, left->mNode, right->mNode) };
  }
  auto lhs =
    ScalarRValueOf(*left, mArena.Get(expr.mA).mLoc, std::format("the left operand of '{}'", ToString(op)));
  if (!lhs) {
    return std::nullopt;
  }
  auto rhs = ScalarRValue(expr.mB, std::format("the right operand of '{}'", ToString(op)));
  if (!rhs) {
    return std::nullopt;
  }

  return CombineBinary(op, *lhs, *rhs, expr.mLoc);
}

BindResult
Binder::CombineBinary(BinaryOp op, NodeId lhs, NodeId rhs, SourceLoc loc) noexcept
{
  if (op == BinaryOp::LogicalAnd || op == BinaryOp::LogicalOr) {
    const NodeId node = AddNode(op == BinaryOp::LogicalAnd ? BoundOp::LogicalAnd : BoundOp::LogicalOr,
      mBool,
      loc,
      ToBool(lhs, loc),
      ToBool(rhs, loc));
    return BoundExpr{ .mNode = node };
  }

  const BoundType &lt = Type(TypeOf(lhs));
  const BoundType &rt = Type(TypeOf(rhs));
  const bool isComparison = op == BinaryOp::Equal || op == BinaryOp::NotEqual || op == BinaryOp::Less ||
                            op == BinaryOp::LessEqual || op == BinaryOp::Greater || op == BinaryOp::GreaterEqual;

  if (lt.mKind == TypeKind::Pointer || rt.mKind == TypeKind::Pointer) {
    if (!isComparison) {
      return BindPointerArithmetic(op, lhs, rhs, loc);
    }
    // Pointers compare as addresses
    const NodeId a = ConvertTo(lhs, mU64, loc);
    const NodeId b = ConvertTo(rhs, mU64, loc);
    BoundOp boundOp = BoundOp::Equal;
    switch (op) {
    case BinaryOp::Equal:
      boundOp = BoundOp::Equal;
      break;
    case BinaryOp::NotEqual:
      boundOp = BoundOp::NotEqual;
      break;
    case BinaryOp::Less:
      boundOp = BoundOp::Less;
      break;
    case BinaryOp::LessEqual:
      boundOp = BoundOp::LessEqual;
      break;
    case BinaryOp::Greater:
      boundOp = BoundOp::Greater;
      break;
    default:
      boundOp = BoundOp::GreaterEqual;
      break;
    }
    return BoundExpr{ .mNode = AddNode(boundOp, mBool, loc, a, b) };
  }

  TypeId common = CommonArithmeticType(TypeOf(lhs), TypeOf(rhs));
  const bool isFloat = Type(common).mKind == TypeKind::Float;
  const bool integerOnly = op == BinaryOp::Mod || op == BinaryOp::Shl || op == BinaryOp::Shr ||
                           op == BinaryOp::BitAnd || op == BinaryOp::BitOr || op == BinaryOp::BitXor;
  if (integerOnly && isFloat) {
    return FailExpr(loc, std::format("operator '{}' needs integer operands", ToString(op)));
  }
  if (op == BinaryOp::Shl || op == BinaryOp::Shr) {
    // The type of a shift is the (promoted) type of its left operand
    common = CommonArithmeticType(TypeOf(lhs), TypeOf(lhs));
  }

  const NodeId a = ConvertTo(lhs, common, loc);
  const NodeId b = ConvertTo(rhs, op == BinaryOp::Shl || op == BinaryOp::Shr ? mI64 : common, loc);

  BoundOp boundOp{};
  switch (op) {
  case BinaryOp::Add:
    boundOp = BoundOp::Add;
    break;
  case BinaryOp::Sub:
    boundOp = BoundOp::Sub;
    break;
  case BinaryOp::Mul:
    boundOp = BoundOp::Mul;
    break;
  case BinaryOp::Div:
    boundOp = BoundOp::Div;
    break;
  case BinaryOp::Mod:
    boundOp = BoundOp::Mod;
    break;
  case BinaryOp::Shl:
    boundOp = BoundOp::Shl;
    break;
  case BinaryOp::Shr:
    boundOp = BoundOp::Shr;
    break;
  case BinaryOp::BitAnd:
    boundOp = BoundOp::BitAnd;
    break;
  case BinaryOp::BitOr:
    boundOp = BoundOp::BitOr;
    break;
  case BinaryOp::BitXor:
    boundOp = BoundOp::BitXor;
    break;
  case BinaryOp::Equal:
    boundOp = BoundOp::Equal;
    break;
  case BinaryOp::NotEqual:
    boundOp = BoundOp::NotEqual;
    break;
  case BinaryOp::Less:
    boundOp = BoundOp::Less;
    break;
  case BinaryOp::LessEqual:
    boundOp = BoundOp::LessEqual;
    break;
  case BinaryOp::Greater:
    boundOp = BoundOp::Greater;
    break;
  case BinaryOp::GreaterEqual:
    boundOp = BoundOp::GreaterEqual;
    break;
  case BinaryOp::LogicalAnd:
  case BinaryOp::LogicalOr:
    break;
  }
  return BoundExpr{ .mNode = AddNode(boundOp, isComparison ? mBool : common, loc, a, b) };
}

BindResult
Binder::BindUnary(const Expr &expr) noexcept
{
  const auto op = expr.Unary();
  auto operand = Bind(expr.mA);
  if (!operand) {
    return std::nullopt;
  }

  if (op == UnaryOp::AddressOf) {
    const Expr &operandExpr = mArena.Get(expr.mA);
    if (operandExpr.mKind == ExprKind::Identifier) {
      const ScopeEntry *entry = LookupName(operandExpr.mName);
      if (entry != nullptr && entry->mKind == ScopeEntry::Kind::Local) {
        return FailExpr(expr.mLoc, "can't take the address of a local");
      }
    }
    if (!operand->mIsLValue) {
      return FailExpr(expr.mLoc, "can only take the address of an lvalue");
    }
    if (mCodeObject.mNodes[operand->mNode].mBitSize > 0) {
      return FailExpr(expr.mLoc, "can't take the address of a bitfield");
    }
    return BoundExpr{ .mNode = AddNode(
                        BoundOp::AddressOf, PointerTo(TypeOf(operand->mNode)), expr.mLoc, operand->mNode) };
  }

  auto value = RValue(*operand, expr.mLoc);
  if (!value) {
    return std::nullopt;
  }
  const TypeId typeId = TypeOf(*value);
  const BoundType &type = Type(typeId);

  switch (op) {
  case UnaryOp::Deref: {
    if (type.mKind != TypeKind::Pointer) {
      return FailExpr(expr.mLoc, std::format("can't dereference a value of type '{}'", type.mName));
    }
    if (Type(type.mElement).mKind == TypeKind::Void) {
      return FailExpr(expr.mLoc, std::format("can't dereference '{}'", type.mName));
    }
    const NodeId node = AddNode(BoundOp::Deref, type.mElement, expr.mLoc, *value);
    return AutoDeref(BoundExpr{ .mNode = node, .mIsLValue = true }, expr.mLoc);
  }
  case UnaryOp::LogicalNot:
    if (!type.IsScalar()) {
      return FailExpr(expr.mLoc, std::format("operator '!' can't be applied to '{}'", type.mName));
    }
    return BoundExpr{ .mNode = AddNode(BoundOp::LogicalNot, mBool, expr.mLoc, *value) };
  case UnaryOp::Plus:
  case UnaryOp::Negate:
  case UnaryOp::BitNot: {
    if (!type.IsArithmetic() || (op == UnaryOp::BitNot && type.mKind == TypeKind::Float)) {
      return FailExpr(expr.mLoc, std::format("operator '{}' can't be applied to '{}'", ToString(op), type.mName));
    }
    const TypeId promoted = CommonArithmeticType(typeId, typeId);
    const NodeId converted = ConvertTo(*value, promoted, expr.mLoc);
    if (op == UnaryOp::Plus) {
      return BoundExpr{ .mNode = converted };
    }
    return BoundExpr{
      .mNode = AddNode(op == UnaryOp::Negate ? BoundOp::Negate : BoundOp::BitNot, promoted, expr.mLoc, converted)
    };
  }
  case UnaryOp::AddressOf:
    break;
  }
  return FailExpr(expr.mLoc, "unsupported unary operator");
}

BindResult
Binder::BindTernary(const Expr &expr) noexcept
{
  auto condition = ScalarRValue(expr.mA, "the condition of '?:'");
  if (!condition) {
    return std::nullopt;
  }
  auto whenTrue = Bind(expr.mB);
  auto whenFalse = whenTrue ? Bind(expr.mC) : std::nullopt;
  if (!whenTrue || !whenFalse) {
    return std::nullopt;
  }

  const BoundType &tt = TypeOf(*whenTrue);
  const BoundType &ft = TypeOf(*whenFalse);
  NodeId a = kNoNode;
  NodeId b = kNoNode;
  TypeId resultType = kNoType;
  if (tt.mKind == TypeKind::Text && ft.mKind == TypeKind::Text) {
    a = whenTrue->mNode;
    b = whenFalse->mNode;
    resultType = mTextType;
  } else {
    auto ta = RValue(*whenTrue, mArena.Get(expr.mB).mLoc);
    auto tb = ta ? RValue(*whenFalse, mArena.Get(expr.mC).mLoc) : std::nullopt;
    if (!ta || !tb) {
      return std::nullopt;
    }
    const BoundType &at = Type(TypeOf(*ta));
    const BoundType &bt = Type(TypeOf(*tb));
    if (at.IsArithmetic() && bt.IsArithmetic()) {
      resultType = CommonArithmeticType(TypeOf(*ta), TypeOf(*tb));
    } else if (at.mKind == TypeKind::Pointer && (bt.mKind == TypeKind::Pointer || bt.IsInteger())) {
      resultType = TypeOf(*ta);
    } else if (bt.mKind == TypeKind::Pointer && at.IsInteger()) {
      resultType = TypeOf(*tb);
    } else {
      return FailExpr(expr.mLoc, std::format("incompatible types '{}' and '{}' in '?:'", at.mName, bt.mName));
    }
    a = ConvertTo(*ta, resultType, expr.mLoc);
    b = ConvertTo(*tb, resultType, expr.mLoc);
  }
  return BoundExpr{ .mNode =
                      AddNode(BoundOp::Ternary, resultType, expr.mLoc, ToBool(*condition, expr.mLoc), a, b) };
}

BindResult
Binder::BindCast(const Expr &expr) noexcept
{
  const auto target = ResolveTypeExpr(mArena.mTypes[expr.mType]);
  if (!target) {
    return std::nullopt;
  }
  if (!Type(*target).IsScalar()) {
    return FailExpr(expr.mLoc,
      std::format("can only cast to numbers and pointers, not '{}'; cast to a pointer and dereference instead",
        Type(*target).mName));
  }
  auto value = ScalarRValue(expr.mA, "the operand of a cast");
  if (!value) {
    return std::nullopt;
  }
  const BoundType &from = Type(TypeOf(*value));
  if (Type(*target).mKind == TypeKind::Pointer && from.mKind == TypeKind::Float) {
    return FailExpr(expr.mLoc, "can't cast a floating point value to a pointer");
  }
  return BoundExpr{ .mNode = ConvertTo(*value, *target, expr.mLoc) };
}

const sym::TemplateParameter *
Binder::TemplateArgumentOf(sym::Type *type, u64 index, SourceLoc loc) noexcept
{
  if (type == nullptr) {
    Fail(loc, "template_arg is only available in views");
    return nullptr;
  }
  type->ResolveType();
  const auto params = type->TemplateParameters();
  if (index >= params.size()) {
    Fail(loc,
      params.empty() ? std::format("'{}' has no template arguments", Type(mCodeObject.mRootType).mName)
                     : std::format("'{}' has {} template argument{}; there is no template_arg({})",
                         Type(mCodeObject.mRootType).mName,
                         params.size(),
                         params.size() == 1 ? "" : "s",
                         index));
    return nullptr;
  }
  return &params[index];
}

#define BINDING_FAILURE(Condition, MessageFormat, ...)                                                            \
  if ((Condition)) {                                                                                              \
    Fail(binding.mLoc, std::format(MessageFormat __VA_OPT__(, ) __VA_ARGS__));                                    \
    return std::nullopt;                                                                                          \
  }

std::optional<TypeId>
Binder::BindUsing(const UsingBinding &binding) noexcept
{
  using Kind = UsingBinding::Kind;
  sym::Type *ofType = mMatchedType;
  switch (binding.mKind) {
  case Kind::Type:
    return ResolveTypeExpr(mArena.mTypes[binding.mType]);
  case Kind::TypeOf: {
    // The expression is bound for its type only; it is never evaluated.
    auto bound = Bind(binding.mExpr);
    if (!bound) {
      return std::nullopt;
    }
    const TypeId type = TypeOf(bound->mNode);
    const auto kind = Type(type).mKind;
    BINDING_FAILURE(kind == TypeKind::Text || kind == TypeKind::ArrayProvider,
      "typeof() needs an expression with a C++ type, not '{}'",
      Type(type).mName);
    return type;
  }
  case Kind::TemplateArgumentOf: {
    BINDING_FAILURE(binding.mOf.empty(), "Expected a name for the template argument type variant");
    const ScopeEntry *entry = LookupName(binding.mOf);
    BINDING_FAILURE(
      !entry || entry->mKind != ScopeEntry::Kind::Type, "could not find type from binding: '{}'", binding.mOf);
    ofType = Type(entry->mType).mDwarf;
    BINDING_FAILURE(!ofType, "Could not find bound type for '{}'", binding.mOf);
    ofType->ResolveAlias()->SkipJunk();
    [[fallthrough]];
  }
  case Kind::TemplateArgument: {
    const auto *param = TemplateArgumentOf(ofType, binding.mIndex, binding.mLoc);
    if (param == nullptr) {
      return std::nullopt;
    }
    BINDING_FAILURE(param->IsTemplateValueParameter(),
      "template argument {} of '{}' is a value, not a type; use template_arg({}) in an expression",
      binding.mIndex,
      ofType->mName,
      binding.mIndex);
    return FromDwarf(param->mType);
  }
  }
  return std::nullopt;
}

BindResult
Binder::BindCall(const Expr &expr) noexcept
{
  const std::string_view name = expr.mName;
  const auto args = mArena.Args(expr);

  if (name == "template_arg") {
    if (mMatchedType == nullptr) {
      return FailExpr(expr.mLoc, "template_arg is only available in views");
    }
    // A template value argument, e.g. the N of AutoTArray<T, N>
    const Expr *index = args.size() == 1 && args[0].mName.empty() ? &mArena.Get(args[0].mExpr) : nullptr;
    if (index == nullptr || index->mKind != ExprKind::IntLiteral) {
      return FailExpr(expr.mLoc, "template_arg() takes the index of a template argument, e.g. template_arg(1)");
    }
    const auto *param = TemplateArgumentOf(mMatchedType, index->mInteger, expr.mLoc);
    if (param == nullptr) {
      return std::nullopt;
    }
    if (!param->IsTemplateValueParameter()) {
      return FailExpr(expr.mLoc,
        std::format("template argument {} of '{}' is a type; name it with 'using T = template_arg({})'",
          index->mInteger,
          Type(mCodeObject.mRootType).mName,
          index->mInteger));
    }
    const TypeId type = FromDwarf(param->mType);
    if (!Type(type).IsInteger()) {
      return FailExpr(expr.mLoc, std::format("template argument {} is not an integer", index->mInteger));
    }
    u64 bits = 0;
    std::memcpy(&bits, param->mConstValueBytes.data(), std::min<size_t>(param->mConstValueBytes.size(), 8));
    return BoundExpr{ .mNode = AddNode(
                        BoundNode{ .mOp = BoundOp::IntConst, .mType = type, .mImm = bits, .mLoc = expr.mLoc }) };
  }
  if (name == "typeof") {
    return FailExpr(expr.mLoc, "typeof() names a type; use it as 'using T = typeof(expression)'");
  }
  if (const auto function = mFunctionIndex.find(name); function != mFunctionIndex.end()) {
    if (!EnsureSignature(function->second)) {
      return std::nullopt;
    }
    if (mCodeObject.mProgram.mFunctions[function->second].mReturn == kNoType) {
      return FailExpr(expr.mLoc, std::format("'{}' doesn't return a value, call it as a statement", name));
    }
    auto boundArgs = BindFunctionArguments(expr, function->second);
    if (!boundArgs) {
      return std::nullopt;
    }
    const TypeId returnType = mCodeObject.mProgram.mFunctions[function->second].mReturn;
    return BoundExpr{ .mNode = AddCallNode(expr, function->second, *boundArgs, returnType) };
  }

  std::vector<const CallArg *> positional;
  std::optional<u64> limit;
  for (const auto &arg : args) {
    if (arg.mName.empty()) {
      positional.push_back(&arg);
    } else if (arg.mName == "limit") {
      limit = ConstantArgument(arg);
      if (!limit) {
        return std::nullopt;
      }
    } else {
      return FailExpr(mArena.Get(arg.mExpr).mLoc, std::format("{}() has no argument named '{}'", name, arg.mName));
    }
  }

  const auto pointerArgument = [&](const CallArg &arg, std::string_view what) -> std::optional<NodeId> {
    auto value = ScalarRValue(arg.mExpr, what);
    if (!value) {
      return std::nullopt;
    }
    if (Type(TypeOf(*value)).mKind != TypeKind::Pointer) {
      Fail(mArena.Get(arg.mExpr).mLoc,
        std::format("{} must be a pointer, not '{}'", what, Type(TypeOf(*value)).mName));
      return std::nullopt;
    }
    return value;
  };

  if (name == "array") {
    if (positional.size() != 2) {
      return FailExpr(expr.mLoc, "array() takes 2 arguments: array(pointer, count)");
    }
    auto pointer = pointerArgument(*positional[0], "the first argument of array()");
    if (!pointer) {
      return std::nullopt;
    }
    const TypeId element = Type(TypeOf(*pointer)).mElement;
    {
      const BoundType &elementType = Type(element);
      if (elementType.mKind == TypeKind::Void || elementType.mSize == 0) {
        return FailExpr(expr.mLoc,
          std::format("array() needs a pointer to a sized type, not '{}'", Type(TypeOf(*pointer)).mName));
      }
      if (EnsureDwarfType(element) == nullptr) {
        return FailExpr(expr.mLoc,
          std::format("array() elements of type '{}' can't be displayed: the type is not in the debug information",
            elementType.mName));
      }
    }
    auto count = IntegerRValue(positional[1]->mExpr, "the count of array()");
    if (!count) {
      return std::nullopt;
    }
    std::string providerName = std::format("array of {}", Type(element).mName);
    const TypeId providerType = AddType(
      BoundType{ .mKind = TypeKind::ArrayProvider, .mElement = element, .mName = std::move(providerName) });
    NodeId node = AddNode(BoundOp::ArrayProvider, providerType, expr.mLoc, *pointer, *count);
    mCodeObject.mNodes[node].mImm = limit.value_or(kDefaultArrayLimit);
    return BoundExpr{ .mNode = node };
  }

  const bool isText = name == "text";
  const bool isCString = name == "cstr";
  u8 width = 0;
  if (name == "utf8") {
    width = 1;
  } else if (name == "utf16") {
    width = 2;
  } else if (name == "utf32") {
    width = 4;
  }

  if (isText || isCString || width != 0) {
    if (positional.empty() || positional.size() > 2 || (isCString && positional.size() != 1)) {
      return FailExpr(expr.mLoc,
        isCString ? std::format("cstr() takes 1 argument: cstr(pointer)")
                  : std::format("{}() takes 1 or 2 arguments: {}(pointer[, length])", name, name));
    }
    auto pointer = pointerArgument(*positional[0], std::format("the first argument of {}()", name));
    if (!pointer) {
      return std::nullopt;
    }
    if (width == 0) {
      const BoundType &element = Type(Type(TypeOf(*pointer)).mElement);
      if (!element.IsInteger() || (element.mSize != 1 && element.mSize != 2 && element.mSize != 4)) {
        return FailExpr(expr.mLoc,
          std::format(
            "{}() needs a pointer to 1, 2 or 4 byte characters, not '{}'; use utf8(), utf16() or utf32() "
            "to choose an encoding",
            name,
            Type(TypeOf(*pointer)).mName));
      }
      width = static_cast<u8>(element.mSize);
    }
    NodeId length = kNoNode;
    if (positional.size() == 2) {
      auto len = IntegerRValue(positional[1]->mExpr, std::format("the length of {}()", name));
      if (!len) {
        return std::nullopt;
      }
      length = *len;
    }
    NodeId node = AddNode(BoundOp::Text, mTextType, expr.mLoc, *pointer, length);
    mCodeObject.mNodes[node].mWidth = width;
    mCodeObject.mNodes[node].mImm = limit.value_or(kDefaultTextLimit);
    return BoundExpr{ .mNode = node };
  }

  return FailExpr(
    expr.mLoc, std::format("unknown function '{}'; available are array, text, cstr, utf8, utf16 and utf32", name));
}

TypeId
Binder::ImportType(const CodeObject &from, TypeId id) noexcept
{
  const BoundType type = from.Type(id);
  if (type.mDwarf != nullptr) {
    return FromDwarf(type.mDwarf);
  }
  switch (type.mKind) {
  case TypeKind::Text:
    return mTextType;
  case TypeKind::Pointer:
    return PointerTo(ImportType(from, type.mElement));
  default: {
    BoundType copy = type;
    copy.mElement = kNoType;
    return AddType(std::move(copy));
  }
  }
}

BindResult
Binder::BindViewCall(const Expr &expr) noexcept
{
  const bool isSummary = expr.mKind == ExprKind::ViewSummary;
  auto object = Bind(expr.mA);
  if (!object) {
    return std::nullopt;
  }
  const SourceLoc objectLoc = mArena.Get(expr.mA).mLoc;
  NodeId pointer = kNoNode;
  TypeId record = kNoType;
  if (object->mIsLValue && TypeOf(*object).mKind == TypeKind::Record) {
    record = TypeOf(object->mNode);
    pointer = AddNode(BoundOp::AddressOf, PointerTo(record), objectLoc, object->mNode);
  } else {
    auto value = RValue(*object, objectLoc);
    if (!value) {
      return std::nullopt;
    }
    const BoundType &type = Type(TypeOf(*value));
    if (type.mKind == TypeKind::Pointer && Type(type.mElement).mKind == TypeKind::Record) {
      record = type.mElement;
      pointer = *value;
    }
  }
  if (record == kNoType || Type(record).mDwarf == nullptr) {
    return FailExpr(objectLoc,
      std::format("view() needs a struct, class or union, or a pointer to one, not '{}'", TypeOf(*object).mName));
  }
  sym::Type *dwarf = Type(record).mDwarf;
  const std::string typeName = Type(record).mName;

  // A call on an object of the type being bound runs this view's own code.
  const bool self = mDef != nullptr && dwarf == mMatchedType;
  const CodeObject *callee = &mCodeObject;
  u32 thisOffset = 0;
  if (!self) {
    if (!mViewLookup) {
      return FailExpr(expr.mLoc, "view() is not available here");
    }
    const ViewLookupResult found = mViewLookup(dwarf);
    if (found.mCycle) {
      return FailExpr(expr.mLoc,
        std::format(
          "view() calls between this view and the view for '{}' form a cycle, which isn't supported", typeName));
    }
    if (found.mView == nullptr) {
      return FailExpr(expr.mLoc, std::format("no view for '{}'", typeName));
    }
    if (found.mView->mBindError) {
      return FailExpr(expr.mLoc,
        std::format("the view for '{}' has an error: {}", typeName, found.mView->mBindError->ToString()));
    }
    callee = found.mView;
    thisOffset = found.mView->mRootOffset;
  }

  ViewCallee call{ .mCallee = callee, .mThisOffset = thisOffset, .mStaticType = dwarf };
  if (isSummary) {
    const u32 index = static_cast<u32>(mCodeObject.mViewCallees.size());
    mCodeObject.mViewCallees.push_back(std::move(call));
    NodeId node = AddNode(BoundOp::ViewSummary, mTextType, expr.mLoc, pointer);
    mCodeObject.mNodes[node].mImm = index;
    return BoundExpr{ .mNode = node };
  }

  const std::string_view name = expr.mName;
  const ViewDefinition *calleeDef = self ? mDef : callee->mDef;
  u32 function = kNoFunction;
  if (calleeDef != nullptr) {
    for (u32 i = 0; i < calleeDef->mFunctions.size(); ++i) {
      if (calleeDef->mFunctions[i].mName == name) {
        function = i;
        break;
      }
    }
  }
  if (function == kNoFunction) {
    return FailExpr(expr.mLoc, std::format("the view for '{}' has no function '{}'", typeName, name));
  }
  if (self) {
    function = mFunctionIndex.find(name)->second;
    if (!EnsureSignature(function)) {
      return std::nullopt;
    }
  }
  const FunctionDef &definition = calleeDef->mFunctions[function];
  // Copies: binding arguments adds functions and types to this code object, which may be the callee.
  const Function signature = callee->mProgram.mFunctions[function];
  if (signature.mReturn == kNoType) {
    return FailExpr(expr.mLoc,
      std::format("'{}' of the view for '{}' doesn't return a value; only functions that return a value can be "
                  "called with view()",
        name,
        typeName));
  }
  const auto args = mArena.Args(expr);
  for (const CallArg &arg : args) {
    if (!arg.mName.empty()) {
      return FailExpr(mArena.Get(arg.mExpr).mLoc, std::format("'{}' takes positional arguments", name));
    }
  }
  if (args.size() != definition.mParams.size()) {
    const size_t count = definition.mParams.size();
    return FailExpr(
      expr.mLoc, std::format("'{}' takes {} argument{}, got {}", name, count, count == 1 ? "" : "s", args.size()));
  }
  std::vector<NodeId> bound;
  for (size_t i = 0; i < args.size(); ++i) {
    if (definition.mParams[i].mIsReference) {
      return FailExpr(mArena.Get(args[i].mExpr).mLoc,
        std::format(
          "'{}' is a reference parameter; they can't be passed through view()", definition.mParams[i].mName));
    }
    auto value = ScalarRValue(args[i].mExpr, "a function argument");
    if (!value) {
      return std::nullopt;
    }
    const TypeId parameter =
      self ? signature.mLocalTypes[i + 1] : ImportType(*callee, signature.mLocalTypes[i + 1]);
    bound.push_back(ConvertTo(*value, parameter, mArena.Get(args[i].mExpr).mLoc));
  }
  const TypeId result = self ? signature.mReturn : ImportType(*callee, signature.mReturn);

  call.mFunction = function;
  call.mName = std::string{ name };
  const u32 index = static_cast<u32>(mCodeObject.mViewCallees.size());
  mCodeObject.mViewCallees.push_back(std::move(call));
  const u32 first = static_cast<u32>(mCodeObject.mCallArgs.size());
  mCodeObject.mCallArgs.insert(mCodeObject.mCallArgs.end(), bound.begin(), bound.end());
  return BoundExpr{ .mNode = AddNode(BoundNode{ .mOp = BoundOp::CallView,
                      .mType = result,
                      .mA = first,
                      .mB = static_cast<u32>(bound.size()),
                      .mC = pointer,
                      .mImm = index,
                      .mLoc = expr.mLoc }) };
}

bool
Binder::LowerDeclaration(const Stmt &stmt) noexcept
{
  auto initializer = ScalarRValue(stmt.mExpr, "a local initializer");
  if (!initializer) {
    return false;
  }

  TypeId type = TypeOf(*initializer);
  if (stmt.mType) {
    auto declared = ResolveTypeExpr(mArena.mTypes[*stmt.mType]);
    if (!declared) {
      return false;
    }
    type = *declared;
  }
  if (!Type(type).IsScalar()) {
    return Fail(stmt.mLoc, "locals hold scalars and pointers, use a pointer");
  }
  *initializer = ConvertTo(*initializer, type, stmt.mLoc);

  Scope &scope = mScopes.back();
  if (scope.contains(stmt.mName)) {
    return Fail(stmt.mLoc, std::format("'{}' is already defined", stmt.mName));
  }
  const u32 slot = mCodeGen.AddLocal(mCurrentFunction, type);
  scope.emplace(stmt.mName, ScopeEntry{ .mKind = ScopeEntry::Kind::Local, .mIndex = slot, .mType = type });
  mCodeGen.EmitExpr(*initializer);
  mCodeGen.Emit(Instruction{ .mOp = Op::StoreLocal, .mA = slot, .mLoc = stmt.mLoc });
  return true;
}

bool
Binder::LowerAssignment(const Stmt &stmt) noexcept
{
  const ScopeEntry *entry = LookupName(stmt.mName);
  if (entry == nullptr || entry->mKind != ScopeEntry::Kind::Local) {
    return Fail(stmt.mLoc, "only locals and parameters can be assigned");
  }

  NodeId value = kNoNode;
  const BoundOp localOp = entry->mIsReference ? BoundOp::LoadRef : BoundOp::Local;
  if (stmt.mKind == StmtKind::Increment) {
    const NodeId local =
      AddNode(BoundNode{ .mOp = localOp, .mType = entry->mType, .mImm = entry->mIndex, .mLoc = stmt.mLoc });
    const NodeId one = AddNode(BoundNode{ .mOp = BoundOp::IntConst, .mType = mI64, .mImm = 1, .mLoc = stmt.mLoc });
    auto combined =
      CombineBinary(stmt.mOperator == AssignOp::Add ? BinaryOp::Add : BinaryOp::Sub, local, one, stmt.mLoc);
    if (!combined) {
      return false;
    }
    value = ConvertTo(combined->mNode, entry->mType, stmt.mLoc);
  } else {
    auto rhs = ScalarRValue(stmt.mExpr, "the right-hand side of an assignment");
    if (!rhs) {
      return false;
    }
    if (stmt.mOperator == AssignOp::Set) {
      value = ConvertTo(*rhs, entry->mType, stmt.mLoc);
    } else {
      const NodeId local =
        AddNode(BoundNode{ .mOp = localOp, .mType = entry->mType, .mImm = entry->mIndex, .mLoc = stmt.mLoc });
      auto combined =
        CombineBinary(stmt.mOperator == AssignOp::Add ? BinaryOp::Add : BinaryOp::Sub, local, *rhs, stmt.mLoc);
      if (!combined) {
        return false;
      }
      value = ConvertTo(combined->mNode, entry->mType, stmt.mLoc);
    }
  }

  mCodeGen.EmitExpr(value);
  mCodeGen.Emit(Instruction{
    .mOp = entry->mIsReference ? Op::StoreRef : Op::StoreLocal, .mA = entry->mIndex, .mLoc = stmt.mLoc });
  return true;
}

bool
Binder::LowerCallStatement(ExprId exprId, SourceLoc loc) noexcept
{
  const Expr &call = mArena.Get(exprId);
  const auto function = mFunctionIndex.find(call.mName);
  if (function == mFunctionIndex.end()) {
    return Fail(loc, std::format("unknown function '{}'", call.mName));
  }
  if (!EnsureSignature(function->second)) {
    return false;
  }
  const Function &callee = mCodeObject.mProgram.mFunctions[function->second];
  if (callee.mReturn == kNoType &&
      (mCurrentFunction == kNoFunction || !mCodeObject.mProgram.mFunctions[mCurrentFunction].mCanYield)) {
    return Fail(loc,
      std::format(
        "'{}' can yield, it can only be called from a children block or another void function", call.mName));
  }
  auto args = BindFunctionArguments(call, function->second);
  if (!args) {
    return false;
  }
  const TypeId returnType = callee.mReturn;
  const NodeId node = AddCallNode(call, function->second, *args, returnType);
  mCodeGen.EmitExpr(node);
  if (returnType != kNoType) {
    mCodeGen.Emit(Instruction{ .mOp = Op::Pop, .mLoc = loc });
  }
  return true;
}

bool
Binder::LowerStatement(StmtId id) noexcept
{
  const Stmt &stmt = mArena.GetStmt(id);
  switch (stmt.mKind) {
  case StmtKind::Block: {
    mScopes.emplace_back();
    for (const StmtId item : mArena.BlockItems(stmt)) {
      if (!LowerStatement(item)) {
        mScopes.pop_back();
        return false;
      }
    }
    mScopes.pop_back();
    return true;
  }
  case StmtKind::Declare:
    return LowerDeclaration(stmt);
  case StmtKind::Assign:
  case StmtKind::Increment:
    return LowerAssignment(stmt);
  case StmtKind::If: {
    auto condition = ScalarRValue(stmt.mExpr, "an if condition");
    if (!condition) {
      return false;
    }
    mCodeGen.EmitExpr(ToBool(*condition, stmt.mLoc));
    const u32 elseJump = mCodeGen.Emit(Instruction{ .mOp = Op::JumpIfFalse, .mLoc = stmt.mLoc });
    if (!LowerStatement(stmt.mA)) {
      return false;
    }
    if (stmt.mB == kNoStmt) {
      mCodeGen.PatchTarget(elseJump, mCodeGen.Here());
      return true;
    }
    const u32 endJump = mCodeGen.Emit(Instruction{ .mOp = Op::Jump, .mLoc = stmt.mLoc });
    mCodeGen.PatchTarget(elseJump, mCodeGen.Here());
    if (!LowerStatement(stmt.mB)) {
      return false;
    }
    mCodeGen.PatchTarget(endJump, mCodeGen.Here());
    return true;
  }
  case StmtKind::While: {
    const u32 top = mCodeGen.Here();
    auto condition = ScalarRValue(stmt.mExpr, "a while condition");
    if (!condition) {
      return false;
    }
    mCodeGen.EmitExpr(ToBool(*condition, stmt.mLoc));
    const u32 endJump = mCodeGen.Emit(Instruction{ .mOp = Op::JumpIfFalse, .mLoc = stmt.mLoc });
    mLoops.emplace_back();
    if (!LowerStatement(stmt.mA)) {
      mLoops.pop_back();
      return false;
    }
    mCodeGen.Emit(Instruction{ .mOp = Op::Jump, .mImm = top, .mLoc = stmt.mLoc });
    const u32 end = mCodeGen.Here();
    mCodeGen.PatchTarget(endJump, end);
    for (const u32 jump : mLoops.back().mBreaks) {
      mCodeGen.PatchTarget(jump, end);
    }
    for (const u32 jump : mLoops.back().mContinues) {
      mCodeGen.PatchTarget(jump, top);
    }
    mLoops.pop_back();
    return true;
  }
  case StmtKind::For: {
    mScopes.emplace_back();
    if (stmt.mA != kNoStmt && !LowerStatement(stmt.mA)) {
      mScopes.pop_back();
      return false;
    }
    const u32 top = mCodeGen.Here();
    u32 endJump = kNoFunction;
    if (stmt.mExpr != kNoExpr) {
      auto condition = ScalarRValue(stmt.mExpr, "a for condition");
      if (!condition) {
        mScopes.pop_back();
        return false;
      }
      mCodeGen.EmitExpr(ToBool(*condition, stmt.mLoc));
      endJump = mCodeGen.Emit(Instruction{ .mOp = Op::JumpIfFalse, .mLoc = stmt.mLoc });
    }
    mLoops.emplace_back();
    if (!LowerStatement(stmt.mC)) {
      mLoops.pop_back();
      mScopes.pop_back();
      return false;
    }
    const u32 next = mCodeGen.Here();
    if (stmt.mB != kNoStmt && !LowerStatement(stmt.mB)) {
      mLoops.pop_back();
      mScopes.pop_back();
      return false;
    }
    mCodeGen.Emit(Instruction{ .mOp = Op::Jump, .mImm = top, .mLoc = stmt.mLoc });
    const u32 end = mCodeGen.Here();
    if (endJump != kNoFunction) {
      mCodeGen.PatchTarget(endJump, end);
    }
    for (const u32 jump : mLoops.back().mBreaks) {
      mCodeGen.PatchTarget(jump, end);
    }
    for (const u32 jump : mLoops.back().mContinues) {
      mCodeGen.PatchTarget(jump, next);
    }
    mLoops.pop_back();
    mScopes.pop_back();
    return true;
  }
  case StmtKind::Continue:
  case StmtKind::Break: {
    if (mLoops.empty()) {
      return Fail(stmt.mLoc, stmt.mKind == StmtKind::Break ? "break outside a loop" : "continue outside a loop");
    }
    const u32 jump = mCodeGen.Emit(Instruction{ .mOp = Op::Jump, .mLoc = stmt.mLoc });
    if (stmt.mKind == StmtKind::Break) {
      mLoops.back().mBreaks.push_back(jump);
    } else {
      mLoops.back().mContinues.push_back(jump);
    }
    return true;
  }
  case StmtKind::Return:
    if (stmt.mExpr != kNoExpr) {
      if (mCurrentFunction == mCodeObject.mChildrenFunction) {
        return Fail(stmt.mLoc, "a children block can't return a value");
      }
      const Function &function = mCodeObject.mProgram.mFunctions[mCurrentFunction];
      if (function.mReturn == kNoType) {
        return Fail(stmt.mLoc, std::format("'{}' doesn't return a value", function.mName));
      }
      if (Type(function.mReturn).mKind == TypeKind::Text) {
        const std::string name = function.mName;
        auto text = Bind(stmt.mExpr);
        if (!text) {
          return false;
        }
        if (TypeOf(*text).mKind != TypeKind::Text) {
          return Fail(stmt.mLoc, std::format("'{}' returns text, not '{}'", name, TypeOf(*text).mName));
        }
        mCodeGen.EmitExpr(text->mNode);
        mCodeGen.Emit(Instruction{ .mOp = Op::Return, .mA = 1, .mLoc = stmt.mLoc });
        return true;
      }
      auto value = ScalarRValue(stmt.mExpr, "a return value");
      if (!value) {
        return false;
      }
      const NodeId converted = ConvertTo(*value, function.mReturn, stmt.mLoc);
      mCodeGen.EmitExpr(converted);
      mCodeGen.Emit(Instruction{ .mOp = Op::Return, .mA = 1, .mLoc = stmt.mLoc });
      return true;
    }
    if (mCurrentFunction != kNoFunction && mCodeObject.mProgram.mFunctions[mCurrentFunction].mReturn != kNoType) {
      return Fail(stmt.mLoc,
        std::format("'{}' must return a value", mCodeObject.mProgram.mFunctions[mCurrentFunction].mName));
    }
    mCodeGen.Emit(Instruction{ .mOp = Op::Return, .mA = 0, .mLoc = stmt.mLoc });
    return true;
  case StmtKind::Yield: {
    if (!mCodeObject.mProgram.mFunctions[mCurrentFunction].mCanYield) {
      return Fail(stmt.mLoc, "yield is only allowed in a children block or a void function");
    }
    auto value = Bind(stmt.mExpr);
    if (!value) {
      return false;
    }
    bool computed = !value->mIsLValue;
    NodeId yielded = value->mNode;
    if (computed) {
      auto rvalue = RValue(*value, stmt.mLoc);
      if (!rvalue) {
        return false;
      }
      yielded = *rvalue;
      const TypeId rvalueType = TypeOf(yielded);
      if (!Type(rvalueType).IsScalar()) {
        return Fail(stmt.mLoc, std::format("yield needs a scalar value, not '{}'", Type(rvalueType).mName));
      }
    }
    const TypeId type = computed ? TypeOf(yielded) : TypeOf(value->mNode);
    if (EnsureDwarfType(type) == nullptr) {
      if (computed) {
        return Fail(stmt.mLoc,
          std::format(
            "no type for '{}' in the debug info of this object file, cast to a type it has", Type(type).mName));
      }
      return Fail(stmt.mLoc,
        std::format("yielded values of type '{}' can't be displayed: the type is not in the debug information",
          Type(type).mName));
    }
    if (stmt.mKey != kNoExpr) {
      auto key = Bind(stmt.mKey);
      if (!key) {
        return false;
      }
      mCodeGen.EmitExpr(key->mNode);
    }
    mCodeGen.EmitExpr(yielded);
    mCodeGen.Emit(Instruction{
      .mOp = Op::Yield, .mA = stmt.mKey == kNoExpr ? 0u : 1u, .mImm = computed ? 1u : 0u, .mLoc = stmt.mLoc });
    return true;
  }
  case StmtKind::Call: {
    return LowerCallStatement(stmt.mExpr, stmt.mLoc);
  }
  }
  return Fail(stmt.mLoc, "internal error: unsupported statement");
}

BindResult
Binder::Bind(ExprId id) noexcept
{
  const Expr &expr = mArena.Get(id);
  switch (expr.mKind) {
  case ExprKind::IntLiteral: {
    const TypeId type = expr.mInteger > static_cast<u64>(std::numeric_limits<i64>::max()) ? mU64 : mI64;
    return BoundExpr{ .mNode = AddNode(BoundNode{
                        .mOp = BoundOp::IntConst, .mType = type, .mImm = expr.mInteger, .mLoc = expr.mLoc }) };
  }
  case ExprKind::CharLiteral: {
    const TypeId type = FromBuiltin(LookupBuiltinType(std::array{ std::string_view{ "char" } }).value());
    return BoundExpr{ .mNode = AddNode(BoundNode{
                        .mOp = BoundOp::IntConst, .mType = type, .mImm = expr.mInteger, .mLoc = expr.mLoc }) };
  }
  case ExprKind::BoolLiteral:
    return BoundExpr{ .mNode = AddNode(BoundNode{
                        .mOp = BoundOp::IntConst, .mType = mBool, .mImm = expr.mInteger, .mLoc = expr.mLoc }) };
  case ExprKind::NullLiteral: {
    const TypeId voidPointer = PointerTo(FromDwarf(nullptr));
    return BoundExpr{ .mNode =
                        AddNode(BoundNode{ .mOp = BoundOp::IntConst, .mType = voidPointer, .mLoc = expr.mLoc }) };
  }
  case ExprKind::FloatLiteral:
    return BoundExpr{ .mNode = AddNode(BoundNode{
                        .mOp = BoundOp::FloatConst, .mType = mF64, .mFloat = expr.mFloat, .mLoc = expr.mLoc }) };
  case ExprKind::StringLiteral:
    return BoundExpr{
      .mNode = AddNode(BoundNode{
        .mOp = BoundOp::TextConst, .mType = mTextType, .mText = mArena.mStrings[expr.mString], .mLoc = expr.mLoc })
    };
  case ExprKind::This:
    return mEnvironment.ResolveThis(*this, expr);
  case ExprKind::Identifier:
    return BindIdentifier(expr);
  case ExprKind::Member: {
    auto base = Bind(expr.mA);
    if (!base) {
      return std::nullopt;
    }
    if (!base->mIsLValue) {
      return FailExpr(expr.mLoc, std::format("'.{}' needs a struct, class or union on its left", expr.mName));
    }
    if (TypeOf(*base).mKind == TypeKind::Pointer) {
      return FailExpr(expr.mLoc,
        std::format("'{}' is a pointer; use '->' to access its member '{}'", TypeOf(*base).mName, expr.mName));
    }
    return MemberOf(*base, expr.mName, expr.mLoc);
  }
  case ExprKind::Arrow: {
    auto base = Bind(expr.mA);
    if (!base) {
      return std::nullopt;
    }
    auto pointer = RValue(*base, expr.mLoc);
    if (!pointer) {
      return std::nullopt;
    }
    const BoundType &pointerType = Type(TypeOf(*pointer));
    if (pointerType.mKind != TypeKind::Pointer) {
      return FailExpr(expr.mLoc,
        std::format("'->{}' needs a pointer on its left, not '{}'{}",
          expr.mName,
          pointerType.mName,
          pointerType.mKind == TypeKind::Record ? "; use '.'" : ""));
    }
    const NodeId deref = AddNode(BoundOp::Deref, pointerType.mElement, expr.mLoc, *pointer);
    return MemberOf(BoundExpr{ .mNode = deref, .mIsLValue = true }, expr.mName, expr.mLoc);
  }
  case ExprKind::Index: {
    auto base = Bind(expr.mA);
    if (!base) {
      return std::nullopt;
    }
    auto pointer = RValue(*base, expr.mLoc);
    if (!pointer) {
      return std::nullopt;
    }
    const BoundType &pointerType = Type(TypeOf(*pointer));
    if (pointerType.mKind != TypeKind::Pointer) {
      return FailExpr(expr.mLoc, std::format("can't index a value of type '{}'", pointerType.mName));
    }
    // Copies: binding the index may add types, which invalidates references into the type table.
    const TypeId elementId = pointerType.mElement;
    const u32 elementSize = Type(elementId).mSize;
    if (Type(elementId).mKind == TypeKind::Void || elementSize == 0) {
      return FailExpr(expr.mLoc, std::format("can't index '{}'", pointerType.mName));
    }
    auto index = IntegerRValue(expr.mB, "an index");
    if (!index) {
      return std::nullopt;
    }
    NodeId address = AddNode(BoundOp::PointerAdd, TypeOf(*pointer), expr.mLoc, *pointer, *index);
    mCodeObject.mNodes[address].mImm = elementSize;
    const NodeId deref = AddNode(BoundOp::Deref, elementId, expr.mLoc, address);
    return AutoDeref(BoundExpr{ .mNode = deref, .mIsLValue = true }, expr.mLoc);
  }
  case ExprKind::Call:
    return BindCall(expr);
  case ExprKind::ViewCall:
  case ExprKind::ViewSummary:
    return BindViewCall(expr);
  case ExprKind::Unary:
    return BindUnary(expr);
  case ExprKind::Binary:
    return BindBinary(expr);
  case ExprKind::Ternary:
    return BindTernary(expr);
  case ExprKind::Cast:
    return BindCast(expr);
  case ExprKind::SizeofType: {
    const auto type = ResolveTypeExpr(mArena.mTypes[expr.mType]);
    if (!type) {
      return std::nullopt;
    }
    return BoundExpr{ .mNode = AddNode(BoundNode{
                        .mOp = BoundOp::IntConst, .mType = mU64, .mImm = Type(*type).mSize, .mLoc = expr.mLoc }) };
  }
  case ExprKind::SizeofExpr: {
    auto operand = Bind(expr.mA);
    if (!operand) {
      return std::nullopt;
    }
    return BoundExpr{
      .mNode = AddNode(
        BoundNode{ .mOp = BoundOp::IntConst, .mType = mU64, .mImm = TypeOf(*operand).mSize, .mLoc = expr.mLoc })
    };
  }
  }
  return FailExpr(expr.mLoc, "unsupported expression");
}

bool
Binder::BindTemplate(const SummarySpec &summary) noexcept
{
  mCodeObject.mSummaryKind = summary.mKind;
  mCodeObject.mSummaryFormat = summary.mFormat;
  for (const auto &part : summary.mParts) {
    BoundTemplatePart boundPart{ .mLiteral = part.mLiteral, .mFormat = part.mFormat };
    NodeId partNode = kNoNode;
    if (part.mExpr != kNoExpr) {
      auto bound = Bind(part.mExpr);
      if (!bound) {
        return false;
      }
      partNode = bound->mNode;
    }
    mCodeObject.mSummaryParts.push_back(boundPart);
    mSummaryPartNodes.push_back(partNode);
  }
  return true;
}

bool
Binder::BindFrame(const SummarySpec &summary) noexcept
{
  mCodeLoc = summary.mLoc;
  mCodeObject.mRootType = kNoType;
  mCodeObject.mThisType = PointerTo(AddType(BoundType{ .mKind = TypeKind::Void, .mName = "void" }));
  mScopes.emplace_back();
  mEnvironment.PopulateGlobalScope(*this, mScopes.back());
  return !mError && BindTemplate(summary);
}

bool
Binder::BindAll() noexcept
{
  assert(mDef && mMatchedType);
  mCodeLoc = mDef->mLoc;
  mCodeObject.mRootType = FromDwarf(mMatchedType);
  mCodeObject.mThisType = PointerTo(mCodeObject.mRootType);
  mCodeObject.mRootTypes = { mCodeObject.mRootType };

  mScopes.emplace_back();
  mEnvironment.PopulateGlobalScope(*this, mScopes.back());
  if (mError) {
    return false;
  }

  for (u32 i = 0; i < mDef->mFunctions.size(); ++i) {
    if (!EnsureSignature(i)) {
      return false;
    }
  }
  for (u32 i = 0; i < mDef->mFunctions.size(); ++i) {
    const FunctionDef &definition = mDef->mFunctions[i];
    mCurrentFunction = mFunctionIndex.find(definition.mName)->second;
    mCodeObject.mProgram.mFunctions[mCurrentFunction].mEntry = mCodeGen.Here();
    mLoops.clear();
    mScopes.emplace_back();
    for (size_t p = 0; p < definition.mParams.size(); ++p) {
      const Parameter &parameter = definition.mParams[p];
      const TypeId type = mCodeObject.mProgram.mFunctions[mCurrentFunction].mLocalTypes[p + 1];
      mScopes.back().emplace(parameter.mName,
        ScopeEntry{
          .mKind = ScopeEntry::Kind::Local,
          .mIsReference = parameter.mIsReference,
          .mIndex = static_cast<u32>(p + 1),
          .mType = type,
        });
    }
    if (!LowerStatement(definition.mBody)) {
      mScopes.pop_back();
      return false;
    }
    mScopes.pop_back();
    mLoops.clear();
    if (mCodeObject.mProgram.mFunctions[mCurrentFunction].mReturn == kNoType) {
      mCodeGen.Emit(Instruction{ .mOp = Op::Return, .mA = 0, .mLoc = definition.mLoc });
    } else {
      mCodeGen.EmitFail(
        std::format("function '{}' ended without returning a value", definition.mName), definition.mLoc);
    }
  }
  mCurrentFunction = kNoFunction;

  const auto &summary = mDef->mSummary;
  mCodeObject.mSummaryKind = summary.mKind;
  mCodeObject.mSummaryFormat = summary.mFormat;
  if (summary.mKind == SummarySpec::Kind::Expression) {
    auto bound = Bind(summary.mExpr);
    if (!bound) {
      return false;
    }
    mSummaryNode = bound->mNode;
  } else if (summary.mKind == SummarySpec::Kind::Template) {
    if (!BindTemplate(summary)) {
      return false;
    }
  }

  const auto &children = mDef->mChildren;
  bool expressionCall = false;
  ExprId expressionCallId = kNoExpr;
  if (children.mKind == ChildrenSpec::Kind::Expression && children.mExpr != kNoExpr) {
    const Expr &expr = mArena.Get(children.mExpr);
    expressionCall = expr.mKind == ExprKind::Call && mFunctionIndex.contains(expr.mName);
    expressionCallId = children.mExpr;
  }
  if (expressionCall) {
    const auto function = mFunctionIndex.find(mArena.Get(expressionCallId).mName);
    if (function != mFunctionIndex.end()) {
      if (!EnsureSignature(function->second)) {
        return false;
      }
      if (mCodeObject.mProgram.mFunctions[function->second].mReturn != kNoType) {
        return Fail(children.mLoc, "children needs a void function");
      }
    }
  }

  mCodeObject.mChildrenKind = children.mKind;
  if (children.mKind == ChildrenSpec::Kind::Expression && !expressionCall) {
    auto bound = Bind(children.mExpr);
    if (!bound) {
      return false;
    }
    if (TypeOf(*bound).mKind != TypeKind::ArrayProvider) {
      return Fail(children.mLoc, "children must be 'none', 'default' or array(pointer, count)");
    }
    mChildrenNode = bound->mNode;
  } else if (children.mKind == ChildrenSpec::Kind::Block || expressionCall) {
    mCurrentFunction = mCodeGen.BeginFunction("children", true);
    mCodeObject.mChildrenFunction = mCurrentFunction;
    mScopes.emplace_back();
    if (expressionCall) {
      if (!LowerCallStatement(expressionCallId, mArena.Get(expressionCallId).mLoc)) {
        mScopes.pop_back();
        return false;
      }
    } else {
      if (!LowerStatement(children.mBody)) {
        mScopes.pop_back();
        return false;
      }
    }
    mScopes.pop_back();
    mCodeGen.Emit(Instruction{ .mOp = Op::Return, .mA = 0, .mLoc = children.mLoc });
    mCodeObject.mChildrenKind = ChildrenSpec::Kind::Block;
  }
  return true;
}

void
Binder::EmitCode() noexcept
{
  if (mDef) {
    for (size_t i = 0; i < mLetNodes.size(); ++i) {
      mCodeObject.mLetFunctions.push_back(
        mCodeGen.EmitExprFunction(std::format("let {}", mDef->mLets[i].mName), mLetNodes[i]));
    }
  }
  if (mCodeObject.mSummaryKind == SummarySpec::Kind::Template) {
    for (size_t i = 0; i < mCodeObject.mSummaryParts.size(); ++i) {
      auto &part = mCodeObject.mSummaryParts[i];
      if (mSummaryPartNodes[i] != kNoNode) {
        part.mFunction = mCodeGen.EmitExprFunction("summary", mSummaryPartNodes[i]);
      }
    }
  } else if (mCodeObject.mSummaryKind == SummarySpec::Kind::Expression) {
    mCodeObject.mSummaryFunction = mCodeGen.EmitExprFunction("summary", mSummaryNode);
  }
  if (mDef && mCodeObject.mChildrenKind == ChildrenSpec::Kind::Expression) {
    mCodeObject.mChildrenFunction = mCodeGen.EmitExprFunction("children", mChildrenNode);
  }
  if (mCodeGen.Error()) {
    mCodeObject.mBindError = Diagnostic{ .mFile = mUnit.mPath, .mLoc = mCodeLoc, .mMessage = *mCodeGen.Error() };
  }
}

} // namespace

std::unique_ptr<CodeObject>
BindView(const ViewDefinition &view, sym::Type *matchedType, u32 rootOffset, const ViewLookup &lookup) noexcept
{
  auto bound = std::make_unique<CodeObject>();
  bound->mSource = view.mSource;
  bound->mDef = &view;
  bound->mMatchedType = matchedType;
  bound->mRootOffset = rootOffset;

  ViewEnvironment environment{ view };
  Binder binder{
    *view.mSource, &view, *bound, matchedType, matchedType->DefiningObjectFile(), environment, lookup
  };
  if (!binder.BindAll()) {
    bound->mBindError = std::move(binder.mError);
  } else {
    binder.EmitCode();
  }
  return bound;
}

u32
FrameEnvironment::RootFor(Binder &binder, u32 variable) noexcept
{
  if (auto found = mRootOf.find(variable); found != mRootOf.end()) {
    return found->second;
  }
  const u32 root = static_cast<u32>(mRootVariables.size());
  mRootOf.emplace(variable, root);
  mRootVariables.push_back(variable);
  binder.mCodeObject.mRootTypes.push_back(binder.FromDwarf(mVariables[variable].mType));
  binder.mCodeObject.mRootNames.emplace_back(mVariables[variable].mName);
  return root;
}

BoundExpr
FrameEnvironment::VariableExpr(Binder &binder, u32 variable, SourceLoc loc) noexcept
{
  const u32 root = RootFor(binder, variable);
  return BoundExpr{
    .mNode = binder.AddNode(
      BoundNode{ .mOp = BoundOp::Root, .mType = binder.mCodeObject.mRootTypes[root], .mImm = root, .mLoc = loc }),
    .mIsLValue = true
  };
}

BindResult
FrameEnvironment::ResolveUnknownName(Binder &binder, const Expr &expr) noexcept
{
  for (u32 i = 0; i < mVariables.size(); ++i) {
    if (mVariables[i].mName == expr.mName) {
      return VariableExpr(binder, i, expr.mLoc);
    }
  }
  // An implicit member access through the frame's 'this' pointer, like in a member function.
  if (mThisVariable) {
    const TypeId pointerType = binder.FromDwarf(mVariables[*mThisVariable].mType);
    const TypeId recordType = binder.Type(pointerType).mElement;
    if (binder.Type(pointerType).mKind == TypeKind::Pointer && binder.Type(recordType).mKind == TypeKind::Record &&
        binder.Type(recordType).mDwarf && binder.FindField(binder.Type(recordType).mDwarf, expr.mName)) {
      auto pointer = binder.RValue(VariableExpr(binder, *mThisVariable, expr.mLoc), expr.mLoc);
      if (!pointer) {
        return std::nullopt;
      }
      const NodeId object = binder.AddNode(BoundOp::Deref, recordType, expr.mLoc, *pointer);
      return binder.MemberOf(BoundExpr{ .mNode = object, .mIsLValue = true }, expr.mName, expr.mLoc);
    }
  }
  return binder.FailExpr(expr.mLoc, std::format("no variable '{}' in this frame", expr.mName));
}

// In a frame template, 'this' is the frame's this pointer.
BindResult
FrameEnvironment::ResolveThis(Binder &binder, const Expr &expr) noexcept
{
  if (!mThisVariable) {
    return binder.FailExpr(expr.mLoc, "no 'this' in this frame");
  }
  return VariableExpr(binder, *mThisVariable, expr.mLoc);
}

BoundFrameTemplate
BindFrameTemplate(std::unique_ptr<FrameTemplate> tmpl,
  std::span<const FrameVariable> variables,
  ObjectFile *objectFile,
  const ViewLookup &lookup) noexcept
{
  BoundFrameTemplate result;
  result.mTemplate = std::move(tmpl);
  result.mCodeObject = std::make_unique<CodeObject>();
  result.mCodeObject->mSource = &result.mTemplate->mUnit;
  if (!result.mTemplate->mUnit.mDiagnostics.empty()) {
    result.mCodeObject->mBindError = result.mTemplate->mUnit.mDiagnostics.front();
    return result;
  }
  FrameEnvironment environment{ variables };
  Binder binder{ result.mTemplate->mUnit, nullptr, *result.mCodeObject, nullptr, objectFile, environment, lookup };
  if (!binder.BindFrame(result.mTemplate->mSummary)) {
    result.mCodeObject->mBindError = std::move(binder.mError);
  } else {
    binder.EmitCode();
  }
  if (!result.mCodeObject->mBindError) {
    result.mRootVariables = std::move(environment.mRootVariables);
  }
  return result;
}

} // namespace mdb::views
