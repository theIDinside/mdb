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
  u32 mIndex{ 0 };
  TypeId mType{ kNoType };
  bool mIsLValue{ false };
  bool mIsReference{ false };
};

using Scope = std::unordered_map<std::string_view, ScopeEntry>;

class Binder;

class BindingEnvironment
{
public:
  virtual ~BindingEnvironment() noexcept = default;
  virtual void PopulateGlobalScope(Binder &binder, Scope &scope) noexcept = 0;
  virtual BindResult ResolveUnknownName(Binder &binder, const Expr &expr) noexcept = 0;
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

  const ViewDefinition &mDef;
  const ExprArena &mArena;
  BoundView &mView;
  sym::Type *mMatchedType;
  ObjectFile *mObjectFile;
  BindingEnvironment &mEnvironment;
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

  Binder(const ViewDefinition &def, BoundView &view, sym::Type *matchedType, BindingEnvironment &environment)
      : mDef(def), mArena(def.mFile->mArena), mView(view), mMatchedType(matchedType),
        mObjectFile(matchedType->DefiningObjectFile()), mEnvironment(environment), mCodeGen(view)
  {
    mI64 = FromBuiltin(BuiltinI64());
    mU64 = FromBuiltin(BuiltinU64());
    mF64 = FromBuiltin(BuiltinF64());
    mBool = FromBuiltin(BuiltinBool());
    mTextType = AddType(BoundType{ .mKind = TypeKind::Text, .mName = "text" });
  }

  bool BindAll() noexcept;
  void EmitCode() noexcept;

private:
  bool
  Fail(SourceLoc loc, std::string message) noexcept
  {
    if (!mError) {
      mError = Diagnostic{ .mFile = mDef.mFile->mPath, .mLoc = loc, .mMessage = std::move(message) };
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
    mView.mTypes.push_back(std::move(type));
    return static_cast<TypeId>(mView.mTypes.size() - 1);
  }

  [[nodiscard]] const BoundType &
  Type(TypeId id) const noexcept
  {
    return mView.mTypes[id];
  }

  [[nodiscard]] TypeId
  TypeOf(NodeId node) const noexcept
  {
    return mView.mNodes[node].mType;
  }

  [[nodiscard]] const BoundType &
  TypeOf(const BoundExpr &expr) const noexcept
  {
    return Type(TypeOf(expr.mNode));
  }

  NodeId
  AddNode(BoundNode node) noexcept
  {
    mView.mNodes.push_back(std::move(node));
    return static_cast<NodeId>(mView.mNodes.size() - 1);
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
      "template_arg", "typeof", "array", "text", "cstr", "utf8", "utf16", "utf32", "sizeof"
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
        ScopeEntry{ .mKind = ScopeEntry::Kind::Let, .mIndex = declaration.mIndex, .mIsLValue = bound->mIsLValue });
    }
  }

  BindResult
  ResolveUnknownName(Binder &binder, const Expr &expr) noexcept override
  {
    const BoundExpr self{ .mNode = binder.AddNode(BoundOp::This, binder.mView.mRootType, expr.mLoc),
      .mIsLValue = true };
    return binder.MemberOf(self, expr.mName, expr.mLoc);
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
  if (mView.mTypes[id].mDwarf == nullptr) {
    if (auto it = mBuiltinDefinitions.find(id); it != mBuiltinDefinitions.end()) {
      mView.mTypes[id].mDwarf = LookupDwarfBaseType(it->second);
    }
  }
  return mView.mTypes[id].mDwarf;
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

  BoundType result{ .mSize = type->Size(), .mDwarf = type, .mName = mView.mTypes[id].mName };
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

  mView.mTypes[id] = std::move(result);
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
  if (mSignatureState[function] == 2) {
    return true;
  }
  if (mSignatureState[function] == 1) {
    return true;
  }
  mSignatureState[function] = 1;
  const FunctionDef &definition = mDef.mFunctions[function];
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
    auto type = ResolveTypeExpr(mArena.mTypes[*definition.mReturnType]);
    if (!type) {
      return false;
    }
    if (!Type(*type).IsScalar()) {
      return Fail(mArena.mTypes[*definition.mReturnType].mLoc,
        std::format("return type of '{}' must be a number or a pointer", definition.mName));
    }
    returnType = *type;
  }
  auto &boundFunction = mView.mProgram.mFunctions[function];
  boundFunction.mParamCount = static_cast<u32>(definition.mParams.size() + 1);
  boundFunction.mReturn = returnType;
  mSignatureState[function] = 2;
  return true;
}

std::optional<std::vector<NodeId>>
Binder::BindFunctionArguments(const Expr &expr, u32 function) noexcept
{
  if (!EnsureSignature(function)) {
    return std::nullopt;
  }
  const FunctionDef &definition = mDef.mFunctions[function];
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
      const TypeId expectedType = mView.mProgram.mFunctions[function].mLocalTypes[i + 1];
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
    const TypeId parameterType = mView.mProgram.mFunctions[function].mLocalTypes[i + 1];
    bound.push_back(ConvertTo(*value, parameterType, mArena.Get(args[i].mExpr).mLoc));
  }
  return bound;
}

NodeId
Binder::AddCallNode(const Expr &expr, u32 function, std::span<const NodeId> args, TypeId returnType) noexcept
{
  const u32 first = static_cast<u32>(mView.mCallArgs.size());
  mView.mCallArgs.insert(mView.mCallArgs.end(), args.begin(), args.end());
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
  const SourceLoc loc = mArena.Get(id).mLoc;
  auto value = RValue(*bound, loc);
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
    mView.mNodes[node].mImm = elementSize(lt);
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
  mView.mNodes[node].mImm = scale;
  return BoundExpr{ .mNode = node };
}

BindResult
Binder::BindBinary(const Expr &expr) noexcept
{
  const auto op = expr.Binary();
  auto lhs = ScalarRValue(expr.mA, std::format("the left operand of '{}'", ToString(op)));
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
    if (mView.mNodes[operand->mNode].mBitSize > 0) {
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
  type->ResolveType();
  const auto params = type->TemplateParameters();
  if (index >= params.size()) {
    Fail(loc,
      params.empty() ? std::format("'{}' has no template arguments", Type(mView.mRootType).mName)
                     : std::format("'{}' has {} template argument{}; there is no template_arg({})",
                         Type(mView.mRootType).mName,
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
          Type(mView.mRootType).mName,
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
    if (mView.mProgram.mFunctions[function->second].mReturn == kNoType) {
      return FailExpr(expr.mLoc, std::format("'{}' doesn't return a value, call it as a statement", name));
    }
    auto boundArgs = BindFunctionArguments(expr, function->second);
    if (!boundArgs) {
      return std::nullopt;
    }
    const TypeId returnType = mView.mProgram.mFunctions[function->second].mReturn;
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
    mView.mNodes[node].mImm = limit.value_or(kDefaultArrayLimit);
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
    mView.mNodes[node].mWidth = width;
    mView.mNodes[node].mImm = limit.value_or(kDefaultTextLimit);
    return BoundExpr{ .mNode = node };
  }

  return FailExpr(
    expr.mLoc, std::format("unknown function '{}'; available are array, text, cstr, utf8, utf16 and utf32", name));
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
  const Function &callee = mView.mProgram.mFunctions[function->second];
  if (callee.mReturn == kNoType &&
      (mCurrentFunction == kNoFunction || !mView.mProgram.mFunctions[mCurrentFunction].mCanYield)) {
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
      if (mCurrentFunction == mView.mChildrenFunction) {
        return Fail(stmt.mLoc, "a children block can't return a value");
      }
      const Function &function = mView.mProgram.mFunctions[mCurrentFunction];
      if (function.mReturn == kNoType) {
        return Fail(stmt.mLoc, std::format("'{}' doesn't return a value", function.mName));
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
    if (mCurrentFunction != kNoFunction && mView.mProgram.mFunctions[mCurrentFunction].mReturn != kNoType) {
      return Fail(
        stmt.mLoc, std::format("'{}' must return a value", mView.mProgram.mFunctions[mCurrentFunction].mName));
    }
    mCodeGen.Emit(Instruction{ .mOp = Op::Return, .mA = 0, .mLoc = stmt.mLoc });
    return true;
  case StmtKind::Yield: {
    if (!mView.mProgram.mFunctions[mCurrentFunction].mCanYield) {
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
    return BoundExpr{ .mNode = AddNode(BoundOp::This, mView.mRootType, expr.mLoc), .mIsLValue = true };
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
    mView.mNodes[address].mImm = elementSize;
    const NodeId deref = AddNode(BoundOp::Deref, elementId, expr.mLoc, address);
    return AutoDeref(BoundExpr{ .mNode = deref, .mIsLValue = true }, expr.mLoc);
  }
  case ExprKind::Call:
    return BindCall(expr);
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
Binder::BindAll() noexcept
{
  mView.mRootType = FromDwarf(mMatchedType);
  mView.mThisType = PointerTo(mView.mRootType);
  mView.mRootTypes = { mView.mRootType };

  mScopes.emplace_back();
  mEnvironment.PopulateGlobalScope(*this, mScopes.back());
  if (mError) {
    return false;
  }

  for (u32 i = 0; i < mDef.mFunctions.size(); ++i) {
    if (!EnsureSignature(i)) {
      return false;
    }
  }
  for (u32 i = 0; i < mDef.mFunctions.size(); ++i) {
    const FunctionDef &definition = mDef.mFunctions[i];
    mCurrentFunction = mFunctionIndex.find(definition.mName)->second;
    mView.mProgram.mFunctions[mCurrentFunction].mEntry = mCodeGen.Here();
    mLoops.clear();
    mScopes.emplace_back();
    for (size_t p = 0; p < definition.mParams.size(); ++p) {
      const Parameter &parameter = definition.mParams[p];
      const TypeId type = mView.mProgram.mFunctions[mCurrentFunction].mLocalTypes[p + 1];
      mScopes.back().emplace(parameter.mName,
        ScopeEntry{ .mKind = ScopeEntry::Kind::Local,
          .mIndex = static_cast<u32>(p + 1),
          .mType = type,
          .mIsReference = parameter.mIsReference });
    }
    if (!LowerStatement(definition.mBody)) {
      mScopes.pop_back();
      return false;
    }
    mScopes.pop_back();
    mLoops.clear();
    if (mView.mProgram.mFunctions[mCurrentFunction].mReturn == kNoType) {
      mCodeGen.Emit(Instruction{ .mOp = Op::Return, .mA = 0, .mLoc = definition.mLoc });
    } else {
      mCodeGen.EmitFail(
        std::format("function '{}' ended without returning a value", definition.mName), definition.mLoc);
    }
  }
  mCurrentFunction = kNoFunction;

  const auto &summary = mDef.mSummary;
  mView.mSummaryKind = summary.mKind;
  mView.mSummaryFormat = summary.mFormat;
  if (summary.mKind == SummarySpec::Kind::Expression) {
    auto bound = Bind(summary.mExpr);
    if (!bound) {
      return false;
    }
    mSummaryNode = bound->mNode;
  } else if (summary.mKind == SummarySpec::Kind::Template) {
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
      mView.mSummaryParts.push_back(boundPart);
      mSummaryPartNodes.push_back(partNode);
    }
  }

  const auto &children = mDef.mChildren;
  bool expressionCall = false;
  ExprId expressionCallId = kNoExpr;
  if (children.mKind == ChildrenSpec::Kind::Expression && children.mExpr != kNoExpr) {
    const Expr &expr = mArena.Get(children.mExpr);
    expressionCall = expr.mKind == ExprKind::Call && mFunctionIndex.contains(expr.mName);
    expressionCallId = children.mExpr;
  }
  const Expr *callForm = nullptr;
  if (children.mIsCallForm && children.mBody != kNoStmt) {
    const Stmt &block = mArena.GetStmt(children.mBody);
    if (block.mKind == StmtKind::Block && block.mCount == 1) {
      const Stmt &call = mArena.GetStmt(mArena.BlockItems(block).front());
      if (call.mKind == StmtKind::Call) {
        callForm = &mArena.Get(call.mExpr);
      }
    }
  } else if (expressionCall) {
    callForm = &mArena.Get(expressionCallId);
  }
  if (callForm != nullptr) {
    const auto function = mFunctionIndex.find(callForm->mName);
    if (function != mFunctionIndex.end()) {
      if (!EnsureSignature(function->second)) {
        return false;
      }
      if (mView.mProgram.mFunctions[function->second].mReturn != kNoType) {
        return Fail(children.mLoc, "children needs a void function");
      }
    }
  }

  mView.mChildrenKind = children.mKind;
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
    if (children.mSize != kNoExpr) {
      auto size = IntegerRValue(children.mSize, "the size of children");
      if (!size) {
        return false;
      }
      mView.mSizeFunction = mCodeGen.EmitExprFunction("size", *size);
    }
    mView.mChildrenLimit = children.mLimit;
    mCurrentFunction = mCodeGen.BeginFunction("children", true);
    mView.mChildrenFunction = mCurrentFunction;
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
    mView.mChildrenKind = ChildrenSpec::Kind::Block;
  }
  return true;
}

void
Binder::EmitCode() noexcept
{
  for (size_t i = 0; i < mLetNodes.size(); ++i) {
    mView.mLetFunctions.push_back(
      mCodeGen.EmitExprFunction(std::format("let {}", mDef.mLets[i].mName), mLetNodes[i]));
  }
  if (mView.mSummaryKind == SummarySpec::Kind::Template) {
    for (size_t i = 0; i < mView.mSummaryParts.size(); ++i) {
      auto &part = mView.mSummaryParts[i];
      if (mSummaryPartNodes[i] != kNoNode) {
        part.mFunction = mCodeGen.EmitExprFunction("summary", mSummaryPartNodes[i]);
      }
    }
  } else if (mView.mSummaryKind == SummarySpec::Kind::Expression) {
    mView.mSummaryFunction = mCodeGen.EmitExprFunction("summary", mSummaryNode);
  }
  if (mView.mChildrenKind == ChildrenSpec::Kind::Expression) {
    mView.mChildrenFunction = mCodeGen.EmitExprFunction("children", mChildrenNode);
  }
  if (mCodeGen.Error()) {
    mView.mBindError = Diagnostic{ .mFile = mDef.mFile->mPath, .mLoc = mDef.mLoc, .mMessage = *mCodeGen.Error() };
  }
}

} // namespace

std::unique_ptr<BoundView>
BindView(const ViewDefinition &view, sym::Type *matchedType, u32 rootOffset) noexcept
{
  auto bound = std::make_unique<BoundView>();
  bound->mDef = &view;
  bound->mMatchedType = matchedType;
  bound->mRootOffset = rootOffset;

  ViewEnvironment environment{ view };
  Binder binder{ view, *bound, matchedType, environment };
  if (!binder.BindAll()) {
    bound->mBindError = std::move(binder.mError);
  } else {
    binder.EmitCode();
  }
  return bound;
}

} // namespace mdb::views
