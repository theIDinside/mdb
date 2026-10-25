/** LICENSE TEMPLATE */
#include "matcher.h"

// mdb
#include <symbolication/dwarf/die.h>
#include <symbolication/dwarf/die_iterator.h>
#include <symbolication/dwarf/die_ref.h>
#include <symbolication/dwarf_attribute_value.h>
#include <symbolication/objfile.h>
#include <symbolication/type.h>

// std
#include <algorithm>
#include <cstring>
#include <format>
#include <string>

namespace mdb::views {

bool
IsViewableType(sym::Type *type) noexcept
{
  if (type == nullptr) {
    return false;
  }
  switch (type->GetDwarfTag()) {
  case DwarfTag::DW_TAG_structure_type:
  case DwarfTag::DW_TAG_class_type:
  case DwarfTag::DW_TAG_union_type:
    return *type->mModifier == sym::Modifier::None;
  default:
    return false;
  }
}

std::vector<BaseClass>
DirectBaseClasses(sym::Type *type) noexcept
{
  std::vector<BaseClass> bases;
  ObjectFile *objfile = type->DefiningObjectFile();
  if (objfile == nullptr || !type->mCompUnitDieReference->IsValid()) {
    return bases;
  }
  auto die = type->mCompUnitDieReference->ToDieReference();
  const auto *children = die.GetDie()->GetChildren();
  if (children == nullptr) {
    return bases;
  }

  for (const auto &child : sym::dw::IterateSiblings{ die.GetUnitData(), children }) {
    if (child.mTag != DwarfTag::DW_TAG_inheritance) {
      continue;
    }
    const sym::dw::DieReference inheritance{ die.GetUnitData(), &child };
    const auto typeId = inheritance.ReadAttribute(Attribute::DW_AT_type);
    if (!typeId) {
      continue;
    }
    const auto location = inheritance.ReadAttribute(Attribute::DW_AT_data_member_location);
    // Virtual bases have a location expression instead of an offset; we can't view those statically.
    if (location && !location->IsDataForm()) {
      continue;
    }
    auto baseDie = objfile->GetDebugInfoEntryReference(typeId->AsUnsignedValue());
    if (!baseDie) {
      continue;
    }
    sym::Type *base = objfile->GetTypeStorage()->GetOrCreateNewType(baseDie->AsIndexed());
    if (base == nullptr) {
      continue;
    }
    const auto canonical = sym::CanonicalType::From(*base);
    bases.push_back(BaseClass{
      .mType = canonical.TypeEntry(), .mOffset = static_cast<u32>(location ? location->AsUnsignedValue() : 0) });
  }
  return bases;
}

// Inline namespaces (`std::__1`, `std::__cxx11`) are not part of how people write type names. Compilers mark them
// with DW_AT_export_symbols, but not always.
static bool
IsInlineNamespaceName(std::string_view name) noexcept
{
  return name == "__1" || name == "__cxx11" || name == "__2";
}

static bool
IsWordCharacter(char c) noexcept
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

// Whitespace is removed, except where it separates two words: `vector<int, std::allocator<int> >` becomes
// `vector<int,std::allocator<int>>`, but `long unsigned int` stays as it is.
static std::string
NormalizeTypeName(std::string_view name) noexcept
{
  std::string result;
  result.reserve(name.size());
  bool pendingSpace = false;
  for (const char c : name) {
    if (c == ' ' || c == '\t' || c == '\n') {
      pendingSpace = true;
      continue;
    }
    if (pendingSpace && !result.empty() && IsWordCharacter(result.back()) && IsWordCharacter(c)) {
      result.push_back(' ');
    }
    pendingSpace = false;
    result.push_back(c);
  }
  return result;
}

// Scopes enclosing a type, outermost first, with class scopes spelled in full (`Outer<int>`), for
// QualifiedTypeName.
static std::vector<std::string_view>
QualifyingScopes(sym::Type *type) noexcept
{
  std::vector<std::string_view> scopes;
  if (!type->mCompUnitDieReference->IsValid()) {
    return scopes;
  }
  auto parent = type->mCompUnitDieReference->ToDieReference().GetParent();
  while (parent && parent->IsValid()) {
    const auto tag = parent->GetDie()->mTag;
    const bool isNamespace = tag == DwarfTag::DW_TAG_namespace;
    if (!isNamespace && tag != DwarfTag::DW_TAG_structure_type && tag != DwarfTag::DW_TAG_class_type &&
        tag != DwarfTag::DW_TAG_union_type) {
      break;
    }
    const auto name = parent->ReadAttribute(Attribute::DW_AT_name);
    if (name) {
      const std::string_view scopeName = name->AsStringView();
      const bool isInline = isNamespace && (parent->ReadAttribute(Attribute::DW_AT_export_symbols).has_value() ||
                                             IsInlineNamespaceName(scopeName));
      if (!isInline) {
        scopes.push_back(scopeName);
      }
    } else if (isNamespace) {
      scopes.push_back("(anonymous namespace)");
    }
    parent = parent->GetParent();
  }
  std::ranges::reverse(scopes);
  return scopes;
}

static constexpr u32 kMaxSpellingDepth = 8;

static std::string QualifiedTypeName(sym::Type *type, u32 depth) noexcept;

// How a template argument is spelled when we have to rebuild a name. Typedefs are resolved, like compilers do in
// the names they write.
static std::string
TypeSpelling(sym::Type *type, u32 depth) noexcept
{
  if (type == nullptr) {
    return "void";
  }
  if (depth > kMaxSpellingDepth) {
    return "...";
  }
  switch (*type->mModifier) {
  case sym::Modifier::Const:
    return std::format("const {}", TypeSpelling(type->TypeChain(), depth + 1));
  case sym::Modifier::Volatile:
    return std::format("volatile {}", TypeSpelling(type->TypeChain(), depth + 1));
  case sym::Modifier::Pointer:
    return std::format("{}*", TypeSpelling(type->TypeChain(), depth + 1));
  case sym::Modifier::Reference:
    return std::format("{}&", TypeSpelling(type->TypeChain(), depth + 1));
  case sym::Modifier::RValueReference:
    return std::format("{}&&", TypeSpelling(type->TypeChain(), depth + 1));
  case sym::Modifier::Array:
    return std::format("{}[{}]", TypeSpelling(type->TypeChain(), depth + 1), type->ArraySize());
  default:
    break;
  }
  if (*type->mIsTypedef || type->IsDeclaration()) {
    sym::Type *target = sym::CanonicalType::From(*type).TypeEntry();
    if (target != type) {
      return TypeSpelling(target, depth + 1);
    }
  }
  switch (type->GetDwarfTag()) {
  case DwarfTag::DW_TAG_structure_type:
  case DwarfTag::DW_TAG_class_type:
  case DwarfTag::DW_TAG_union_type:
  case DwarfTag::DW_TAG_enumeration_type:
    return QualifiedTypeName(type, depth + 1);
  default:
    return std::string{ type->mName };
  }
}

static std::string
TemplateArgumentSpelling(const sym::TemplateParameter &param, u32 depth) noexcept
{
  if (!param.IsTemplateValueParameter()) {
    return TypeSpelling(param.mType, depth);
  }
  u64 bits = 0;
  std::memcpy(&bits, param.mConstValueBytes.data(), std::min<size_t>(param.mConstValueBytes.size(), 8));
  const bool isSigned = param.mType != nullptr && param.mType->GetBaseType() &&
                        (*param.mType->GetBaseType() == BaseTypeEncoding::DW_ATE_signed ||
                          *param.mType->GetBaseType() == BaseTypeEncoding::DW_ATE_signed_char);
  return isSigned ? std::to_string(static_cast<i64>(bits)) : std::to_string(bits);
}

static std::string
QualifiedTypeName(sym::Type *type, u32 depth) noexcept
{
  const std::string_view written = type->mName;
  if (written.empty()) {
    return {};
  }

  std::string name;
  for (const auto scope : QualifyingScopes(type)) {
    name.append(scope);
    name.append("::");
  }
  name.append(written);

  // -gsimple-template-names: the name has no arguments, the template parameters still have them.
  if (written.find('<') == std::string_view::npos) {
    type->ResolveType();
    const auto params = type->TemplateParameters();
    if (!params.empty()) {
      name.push_back('<');
      for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
          name.push_back(',');
        }
        name.append(TemplateArgumentSpelling(params[i], depth));
      }
      name.push_back('>');
    }
  }
  return NormalizeTypeName(name);
}

std::string
QualifiedTypeName(sym::Type *type) noexcept
{
  return QualifiedTypeName(type, 0);
}

bool
MatchesObjectFile(const ViewDefinition &view, sym::Type *type) noexcept
{
  if (!view.mObjectFile) {
    return true;
  }
  ObjectFile *objfile = type->DefiningObjectFile();
  if (objfile == nullptr) {
    return false;
  }
  const auto &path = objfile->GetFilePath();
  if (view.mObjectFile->find('/') != std::string::npos) {
    return path.string() == *view.mObjectFile;
  }
  return path.filename().string() == *view.mObjectFile;
}

} // namespace mdb::views
