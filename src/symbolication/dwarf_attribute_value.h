/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <common/formatter.h>
#include <symbolication/dwarf_defs.h>
#include <tracee_pointer.h>

// std
#include <cstring>

template <typename T> concept UnsignedWord = std::is_same_v<T, u32> || std::is_same_v<T, u64>;

#if defined(COMPILERUSED_GCC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
namespace mdb {
struct StrSlice
{
  const char *ptr;
  u64 size;
};

#define ATTR_CTOR(DataType, field)                                                                                \
  constexpr AttributeValue(DataType data, AttributeForm form, Attribute name) noexcept                            \
      : form{ form }, name{ name }, value{ data }                                                                 \
  {                                                                                                               \
  }

template <typename T>
concept AttributeValueType = std::is_same_v<T, u64> || std::is_same_v<T, i64> || std::is_same_v<T, DataBlock> ||
                             std::is_same_v<T, StrSlice> || std::is_same_v<T, std::string_view> ||
                             std::is_same_v<T, AddrPtr> || std::is_same_v<T, const char *>;

/** Fully-formed attribtue */
struct AttributeValue
{
  template <AttributeValueType T>
  constexpr AttributeValue(T value, AttributeForm form, Attribute name) noexcept
      : form{ form }, name{ name }, value{ value }
  {
  }

  constexpr AttributeValue(const AttributeValue &other) : form(other.form), name(other.name), value(other.value) {}
  constexpr AttributeValue &
  operator=(const AttributeValue &other)
  {
    if (this != &other) {
      std::memcpy((void *)this, &other, sizeof(AttributeValue));
    }
    return *this;
  }

  constexpr AttributeValue(AttributeValue &&other) noexcept
      : form(other.form), name(other.name), value(other.value)
  {
  }
  constexpr AttributeValue &
  operator=(AttributeValue &&other) noexcept
  {
    if (this == &other) {
      return *this;
    }
    form = other.form;
    name = other.name;
    value = other.value;
    return *this;
  }

  bool
  IsDataForm() const noexcept
  {
    using enum AttributeForm;
    switch (form) {
    case AttributeForm::DW_FORM_data2:
      [[fallthrough]];
    case AttributeForm::DW_FORM_data4:
      [[fallthrough]];
    case AttributeForm::DW_FORM_data8:
      [[fallthrough]];
    case AttributeForm::DW_FORM_data1:
      return true;
    default:
      return false;
    }
  }

  std::uintptr_t
  AsAddress() const noexcept
  {
    return value.addr;
  }

  const char *
  AsCString() const noexcept
  {
    return value.str;
  }

  std::string_view
  AsStringView() const noexcept
  {
    return value.str;
  }

  DataBlock
  AsDataBlock() const noexcept
  {
    return value.block;
  }
  u64
  AsUnsignedValue() const noexcept
  {
    return value.u;
  }

  constexpr static u64
  ToUnsignedValue(const AttributeValue &value) noexcept
  {
    return value.AsUnsignedValue();
  }

  constexpr static u64
  AsUnsigned(const AttributeValue &v)
  {
    return v.AsUnsignedValue();
  }

  i64
  AsSignedValue() const noexcept
  {
    return value.i;
  }

  constexpr static std::string_view
  ToStringView(const AttributeValue &v) noexcept
  {
    return v.AsCString();
  }

  // std::uintptr_t address() const noexcept;
  // std::string_view string() const noexcept;
  // DataBlock block() const noexcept;
  // u64 unsigned_value() const noexcept;
  // i64 signed_value() const noexcept;
  AttributeForm form;
  Attribute name;

private:
  union _value
  { // Size = 16 bytes
    constexpr _value(const char *str) noexcept : str{ str } {}
    constexpr _value(DataBlock block) noexcept : block(block) {}
    constexpr _value(u64 u) noexcept : u(u) {}
    constexpr _value(i64 i) noexcept : i(i) {}
    constexpr _value(AddrPtr ptr) noexcept : addr(ptr) {}
    constexpr _value(const _value &other)
    {
      if (this == &other) {
        return;
      }
      std::memcpy((void *)this, &other, sizeof(_value));
    }

    constexpr _value &
    operator=(const _value &other)
    {
      if (this != &other) {
        std::memcpy((void *)this, &other, sizeof(_value));
      }
      return *this;
    }

    DataBlock block;
    // StrSlice str;
    const char *str;
    u64 u;
    i64 i;
    AddrPtr addr;
  } value;
};
} // namespace mdb

#if defined(COMPILERUSED_GCC)
#pragma GCC diagnostic pop
#endif

template <> struct std::formatter<mdb::AttributeValue>
{

  BASIC_PARSE

  template <typename FormatContext>
  auto
  format(const mdb::AttributeValue &value, FormatContext &ctx) const
  {
    using enum mdb::AttributeForm;
    auto out_it = ctx.out();
    out_it = std::format_to(out_it, "AttributeValue {{ name: {}, value: ", mdb::to_str(value.name));
    // Which union member is active is determined by the form, see ReadAttributeValue in debug_info_reader.cpp
    switch (value.form) {
    case DW_FORM_addr:
    case DW_FORM_addrx:
    case DW_FORM_addrx1:
    case DW_FORM_addrx2:
    case DW_FORM_addrx3:
    case DW_FORM_addrx4:
      out_it = std::format_to(out_it, "0x{:x}", value.AsAddress());
      break;
    case DW_FORM_string:
    case DW_FORM_strp:
    case DW_FORM_line_strp:
    case DW_FORM_strx:
    case DW_FORM_strx1:
    case DW_FORM_strx2:
    case DW_FORM_strx3:
    case DW_FORM_strx4:
    case DW_FORM_strp_sup: {
      const char *str = value.AsCString();
      out_it = std::format_to(out_it, "\"{}\"", str != nullptr ? std::string_view{ str } : std::string_view{});
    } break;
    case DW_FORM_block:
    case DW_FORM_block1:
    case DW_FORM_block2:
    case DW_FORM_block4:
    case DW_FORM_exprloc:
    case DW_FORM_data16: {
      const auto block = value.AsDataBlock();
      out_it = std::format_to(out_it, "block[{}] {{", block.size);
      for (u64 i = 0; i < block.size; ++i) {
        out_it = std::format_to(out_it, " {:02x}", block.ptr[i]);
      }
      out_it = std::format_to(out_it, " }}");
    } break;
    case DW_FORM_sdata:
    case DW_FORM_implicit_const:
      out_it = std::format_to(out_it, "0x{:x} (signed)", value.AsSignedValue());
      break;
    default:
      out_it = std::format_to(out_it, "0x{:x}", value.AsUnsignedValue());
      break;
    }
    return std::format_to(out_it, " }}");
  }
};

template <> struct std::formatter<std::optional<mdb::AttributeValue>>
{
  BASIC_PARSE
  template <typename FormatContext>
  auto
  format(const std::optional<mdb::AttributeValue> &value, FormatContext &ctx) const
  {
    if (value) {
      return std::format_to(ctx.out(), "{}", *value);
    }
    return std::format_to(ctx.out(), "<null>");
  }
};