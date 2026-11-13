/** LICENSE TEMPLATE */
#include "dynamic_type.h"

// mdb
#include <interface/tracee_command/supervisor_state.h>
#include <symbolication/dwarf/die.h>
#include <symbolication/dwarf/name_index.h>
#include <symbolication/objfile.h>
#include <symbolication/type.h>
#include <utils/logger.h>
#include <views/matcher.h>

// std
#include <cstring>
#include <cxxabi.h>
#include <memory>

namespace mdb::sym {

// Base class chains deeper than this aren't searched.
static constexpr u32 kMaxBaseDepth = 16;

static bool
IsDynamicClass(Type *type, u32 depth) noexcept
{
  if (type == nullptr || depth > kMaxBaseDepth || !views::IsViewableType(type)) {
    return false;
  }
  ObjectFile *objectFile = type->DefiningObjectFile();
  if (objectFile == nullptr) {
    return false;
  }
  auto &cache = objectFile->GetDynamicTypeCache().mIsDynamic;
  if (const auto it = cache.find(type); it != cache.end()) {
    return it->second;
  }
  type->ResolveType();
  bool dynamic = false;
  for (const auto &field : type->MemberFields()) {
    // clang names it `_vptr$Class`, gcc `_vptr.Class`
    const std::string_view name = field.mName;
    if (name.starts_with("_vptr")) {
      dynamic = true;
      break;
    }
  }
  if (!dynamic) {
    for (const auto &base : views::DirectBaseClasses(type)) {
      if (IsDynamicClass(base.mType, depth + 1)) {
        dynamic = true;
        break;
      }
    }
  }
  cache.emplace(type, dynamic);
  return dynamic;
}

bool
IsDynamicClass(Type *type) noexcept
{
  return IsDynamicClass(type, 0);
}

// Is a class named `baseName` a base of `type` (or `type` itself) at `offset`?
static bool
HasBaseAt(Type *type, std::string_view baseName, u64 offset, u32 depth) noexcept
{
  if (type == nullptr || depth > kMaxBaseDepth) {
    return false;
  }
  if (offset == 0 && views::QualifiedTypeName(type) == baseName) {
    return true;
  }
  for (const auto &base : views::DirectBaseClasses(type)) {
    if (base.mOffset <= offset && HasBaseAt(base.mType, baseName, offset - base.mOffset, depth + 1)) {
      return true;
    }
  }
  return false;
}

std::string
ClassNameOfVtableSymbol(std::string_view mangled) noexcept
{
  if (!mangled.starts_with("_ZTV")) {
    return {};
  }
  int status = 0;
  const std::string copy{ mangled };
  std::unique_ptr<char, decltype(&std::free)> demangled{
    abi::__cxa_demangle(copy.c_str(), nullptr, nullptr, &status), &std::free
  };
  if (status != 0 || !demangled) {
    return {};
  }
  constexpr std::string_view prefix = "vtable for ";
  std::string_view name{ demangled.get() };
  if (!name.starts_with(prefix)) {
    return {};
  }
  name.remove_prefix(prefix.size());
  // QualifiedTypeName leaves out inline namespaces (`std::__1`, `std::__cxx11`)
  std::string result;
  result.reserve(name.size());
  for (size_t i = 0; i < name.size();) {
    bool skipped = false;
    for (const std::string_view inlineNamespace : { "__1::", "__2::", "__cxx11::" }) {
      const bool atScopeStart = i == 0 || (i >= 2 && name.substr(i - 2, 2) == "::");
      if (atScopeStart && name.substr(i).starts_with(inlineNamespace)) {
        i += inlineNamespace.size();
        skipped = true;
        break;
      }
    }
    if (!skipped) {
      result.push_back(name[i++]);
    }
  }
  return views::NormalizeTypeName(result);
}

// The last component of a qualified name, `Inner<int>` in `ns::Outer<a::b>::Inner<int>`.
static std::string_view
LastNameComponent(std::string_view name) noexcept
{
  int depth = 0;
  size_t start = 0;
  for (size_t i = 0; i < name.size(); ++i) {
    const char c = name[i];
    if (c == '<' || c == '(') {
      ++depth;
    } else if (c == '>' || c == ')') {
      --depth;
    } else if (depth == 0 && c == ':' && i + 1 < name.size() && name[i + 1] == ':') {
      start = i + 2;
      ++i;
    }
  }
  return name.substr(start);
}

Type *
FindTypeByQualifiedName(ObjectFile &objectFile, std::string_view qualifiedName) noexcept
{
  auto *index = objectFile.GetNameIndex();
  if (index == nullptr || qualifiedName.empty()) {
    return nullptr;
  }
  const std::string wanted = views::NormalizeTypeName(qualifiedName);
  const std::string_view last = LastNameComponent(wanted);
  // With -gsimple-template-names, the DWARF name has no template arguments.
  const std::string_view withoutArguments = last.substr(0, last.find('<'));
  Type *found = nullptr;
  for (const std::string_view key : { last, withoutArguments }) {
    index->ForEachType(key, [&](const dw::DieNameReference &ref) {
      if (found || !ref.IsValid()) {
        return;
      }
      auto die = ref.cu->GetDieByCacheIndex(ref.die_index);
      if (die.GetDie() == nullptr) {
        return;
      }
      Type *type = objectFile.GetTypeStorage()->GetOrCreateNewType(die.AsIndexed());
      if (type == nullptr) {
        return;
      }
      Type *canonical = CanonicalType::From(*type).TypeEntry();
      if (canonical->IsDeclaration() || !views::IsViewableType(canonical)) {
        return;
      }
      if (views::QualifiedTypeName(canonical) == wanted) {
        found = canonical;
      }
    });
    if (found || key == withoutArguments) {
      break;
    }
  }
  return found;
}

static std::optional<u64>
ReadWord(tc::SupervisorState &supervisor, u64 address) noexcept
{
  if (address == 0) {
    return std::nullopt;
  }
  auto result = supervisor.SafeRead(AddrPtr{ address }, sizeof(u64));
  if (!result.is_expected()) {
    return std::nullopt;
  }
  const auto bytes = result.take_value()->Span();
  if (bytes.size() < sizeof(u64)) {
    return std::nullopt;
  }
  u64 word = 0;
  std::memcpy(&word, bytes.data(), sizeof(u64));
  return word;
}

std::optional<DynamicType>
ResolveDynamicType(tc::SupervisorState &supervisor, Type *staticType, u64 address) noexcept
{
  if (staticType == nullptr || address == 0 || !IsDynamicClass(staticType)) {
    return std::nullopt;
  }
  // In the Itanium ABI, a dynamic class has its vtable pointer at offset 0.
  const auto vptr = ReadWord(supervisor, address);
  if (!vptr || *vptr < 16) {
    return std::nullopt;
  }
  SymbolFile *symbolFile = supervisor.FindObjectByPc(AddrPtr{ *vptr });
  if (symbolFile == nullptr) {
    return std::nullopt;
  }
  ObjectFile *objectFile = symbolFile->GetObjectFile();
  const AddrPtr unrelocated = symbolFile->UnrelocateAddress(AddrPtr{ *vptr });
  const MinSymbol *vtable = objectFile->FindVtableSymbol(unrelocated);
  // offset_to_top and the RTTI pointer come before the address points into the vtable
  if (vtable == nullptr || unrelocated.GetRaw() < vtable->address.GetRaw() + 16) {
    return std::nullopt;
  }
  const auto offsetToTop = ReadWord(supervisor, *vptr - 16);
  if (!offsetToTop) {
    return std::nullopt;
  }
  const i64 offset = static_cast<i64>(*offsetToTop);
  if (offset > 0 || static_cast<u64>(-offset) > address) {
    return std::nullopt;
  }

  auto &cache = objectFile->GetDynamicTypeCache().mByVtable;
  const auto key = std::make_pair(unrelocated.GetRaw(), staticType);
  auto cached = cache.find(key);
  if (cached == cache.end()) {
    Type *dynamic = nullptr;
    const std::string name = ClassNameOfVtableSymbol(vtable->name);
    if (!name.empty()) {
      dynamic = FindTypeByQualifiedName(*objectFile, name);
      if (dynamic != nullptr && !HasBaseAt(dynamic, views::QualifiedTypeName(staticType), -offset, 0)) {
        DBGLOG(core,
          "[views] vtable of '{}' at 0x{:x} isn't the vtable of a '{}' at offset {}",
          name,
          *vptr,
          views::QualifiedTypeName(staticType),
          -offset);
        dynamic = nullptr;
      }
    }
    cached = cache.emplace(key, dynamic).first;
  }
  if (cached->second == nullptr) {
    return std::nullopt;
  }
  return DynamicType{ .mType = cached->second, .mAddress = address + offset };
}

} // namespace mdb::sym
