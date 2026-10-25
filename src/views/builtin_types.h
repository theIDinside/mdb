/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <views/bound_ir.h>

// std
#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace mdb::views {

struct BuiltinType
{
  TypeKind mKind;
  u8 mSize;
  bool mIsChar;
  // How the type is displayed
  std::string_view mName;
  // How compilers name this type in DWARF (DW_TAG_base_type names differ between gcc and clang)
  std::array<std::string_view, 4> mDwarfNames;
};

// Resolve the keywords of a builtin type (`unsigned`, `long`) or an alias (`u32`, `size_t`) to a builtin type.
std::optional<BuiltinType> LookupBuiltinType(std::span<const std::string_view> keywords) noexcept;

BuiltinType BuiltinI64() noexcept;
BuiltinType BuiltinU64() noexcept;
BuiltinType BuiltinF64() noexcept;
BuiltinType BuiltinBool() noexcept;

} // namespace mdb::views
