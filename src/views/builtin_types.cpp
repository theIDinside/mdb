/** LICENSE TEMPLATE */
#include "builtin_types.h"

// std
#include <algorithm>

namespace mdb::views {

using namespace std::string_view_literals;

static constexpr BuiltinType
Int(TypeKind kind, u8 size, std::string_view name, std::array<std::string_view, 4> dwarfNames)
{
  return BuiltinType{ .mKind = kind, .mSize = size, .mIsChar = false, .mName = name, .mDwarfNames = dwarfNames };
}

static constexpr BuiltinType
Char(TypeKind kind, u8 size, std::string_view name, std::array<std::string_view, 4> dwarfNames)
{
  return BuiltinType{ .mKind = kind, .mSize = size, .mIsChar = true, .mName = name, .mDwarfNames = dwarfNames };
}

static constexpr auto kVoid =
  BuiltinType{ .mKind = TypeKind::Void, .mSize = 0, .mIsChar = false, .mName = "void" };
static constexpr auto kBool =
  BuiltinType{ .mKind = TypeKind::Bool, .mSize = 1, .mIsChar = false, .mName = "bool", .mDwarfNames = { "bool" } };
static constexpr auto kChar = Char(TypeKind::SignedInt, 1, "char", { "char" });
static constexpr auto kSChar = Char(TypeKind::SignedInt, 1, "signed char", { "signed char" });
static constexpr auto kUChar = Char(TypeKind::UnsignedInt, 1, "unsigned char", { "unsigned char" });
static constexpr auto kChar8 = Char(TypeKind::UnsignedInt, 1, "char8_t", { "char8_t" });
static constexpr auto kChar16 = Char(TypeKind::UnsignedInt, 2, "char16_t", { "char16_t" });
static constexpr auto kChar32 = Char(TypeKind::UnsignedInt, 4, "char32_t", { "char32_t" });
static constexpr auto kWChar = Char(TypeKind::SignedInt, 4, "wchar_t", { "wchar_t" });
static constexpr auto kI8 = Int(TypeKind::SignedInt, 1, "signed char", { "signed char" });
static constexpr auto kU8 = Int(TypeKind::UnsignedInt, 1, "unsigned char", { "unsigned char" });
static constexpr auto kI16 = Int(TypeKind::SignedInt, 2, "short", { "short", "short int" });
static constexpr auto kU16 =
  Int(TypeKind::UnsignedInt, 2, "unsigned short", { "unsigned short", "short unsigned int" });
static constexpr auto kI32 = Int(TypeKind::SignedInt, 4, "int", { "int" });
static constexpr auto kU32 = Int(TypeKind::UnsignedInt, 4, "unsigned int", { "unsigned int" });
static constexpr auto kI64 =
  Int(TypeKind::SignedInt, 8, "long", { "long", "long int", "long long", "long long int" });
static constexpr auto kU64 = Int(TypeKind::UnsignedInt,
  8,
  "unsigned long",
  { "unsigned long", "long unsigned int", "unsigned long long", "long long unsigned int" });
static constexpr auto kF32 = BuiltinType{
  .mKind = TypeKind::Float, .mSize = 4, .mIsChar = false, .mName = "float", .mDwarfNames = { "float" }
};
static constexpr auto kF64 = BuiltinType{
  .mKind = TypeKind::Float, .mSize = 8, .mIsChar = false, .mName = "double", .mDwarfNames = { "double" }
};

struct Alias
{
  std::string_view mName;
  BuiltinType mType;
};

static constexpr auto kAliases = std::to_array<Alias>({
  { "u8", kU8 },
  { "u16", kU16 },
  { "u32", kU32 },
  { "u64", kU64 },
  { "i8", kI8 },
  { "i16", kI16 },
  { "i32", kI32 },
  { "i64", kI64 },
  { "f32", kF32 },
  { "f64", kF64 },
  { "uint8_t", kU8 },
  { "uint16_t", kU16 },
  { "uint32_t", kU32 },
  { "uint64_t", kU64 },
  { "int8_t", kI8 },
  { "int16_t", kI16 },
  { "int32_t", kI32 },
  { "int64_t", kI64 },
  { "size_t", kU64 },
  { "uintptr_t", kU64 },
  { "ssize_t", kI64 },
  { "intptr_t", kI64 },
  { "ptrdiff_t", kI64 },
  { "void", kVoid },
  { "bool", kBool },
  { "char8_t", kChar8 },
  { "char16_t", kChar16 },
  { "char32_t", kChar32 },
  { "wchar_t", kWChar },
  { "float", kF32 },
  { "double", kF64 },
});

std::optional<BuiltinType>
LookupBuiltinType(std::span<const std::string_view> keywords) noexcept
{
  if (keywords.size() == 1) {
    if (const auto *it = std::ranges::find(kAliases, keywords[0], &Alias::mName); it != kAliases.end()) {
      return it->mType;
    }
  }

  int isSigned = 0;
  int isUnsigned = 0;
  int shorts = 0;
  int longs = 0;
  int chars = 0;
  int ints = 0;
  for (const auto word : keywords) {
    if (word == "signed") {
      ++isSigned;
    } else if (word == "unsigned") {
      ++isUnsigned;
    } else if (word == "short") {
      ++shorts;
    } else if (word == "long") {
      ++longs;
    } else if (word == "char") {
      ++chars;
    } else if (word == "int") {
      ++ints;
    } else {
      // a keyword that can't be combined (`char16_t`, `float`, ...) in a multi-keyword type
      return std::nullopt;
    }
  }

  if (isSigned + isUnsigned > 1 || ints > 1 || chars > 1 || shorts > 1 || longs > 2 || (shorts && longs) ||
      (chars && (shorts || longs || ints))) {
    return std::nullopt;
  }

  if (chars) {
    return isUnsigned ? kUChar : (isSigned ? kSChar : kChar);
  }
  if (shorts) {
    return isUnsigned ? kU16 : kI16;
  }
  if (longs) {
    return isUnsigned ? kU64 : kI64;
  }
  return isUnsigned ? kU32 : kI32;
}

BuiltinType
BuiltinI64() noexcept
{
  return kI64;
}

BuiltinType
BuiltinU64() noexcept
{
  return kU64;
}

BuiltinType
BuiltinF64() noexcept
{
  return kF64;
}

BuiltinType
BuiltinBool() noexcept
{
  return kBool;
}

} // namespace mdb::views
