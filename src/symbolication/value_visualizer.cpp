/** LICENSE TEMPLATE */
#include "value_visualizer.h"

// mdb
#include <symbolication/dwarf/die.h>
#include <symbolication/dwarf/typeread.h>
#include <symbolication/objfile.h>
#include <symbolication/type.h>
#include <symbolication/value.h>
#include <symbolication/variable_reference.h>
#include <task.h>
#include <tracer.h>
#include <utils/enumerator.h>
#include <utils/todo.h>

// std
#include <algorithm>
#include <iterator>
#include <memory_resource>
#include <string>
#include <utility>

namespace mdb::sym {

#define FormatAndReturn(result, formatString, ...)                                                                \
  std::format_to(std::back_inserter(result), formatString __VA_OPT__(, ) __VA_ARGS__);                            \
  return result

std::vector<Ref<Value>>
ResolveReference::Resolve(const VariableContext &context, ValueRange valueRange) noexcept
{
  std::vector<Ref<Value>> results;
  auto value = *context.GetValue();
  // actual `T` type behind the reference

  CanonicalType derefType = value.GetType().DerefType();
  if (const auto address = value.ToRemotePointer(); address.has_value()) {
    auto adjusted_address = address.value() + (valueRange.start.value_or(0) * derefType->Size());
    const auto requestedLengthInBytes = valueRange.count.value_or(1) * derefType->Size();
    auto memory = sym::MemoryContentsObject::ReadMemory(
      *context.mTask->GetSupervisor(), adjusted_address, requestedLengthInBytes);
    if (!memory.is_ok()) {
      results.push_back(Ref<Value>::MakeShared(
        nullptr, *derefType.TypeEntry(), 0U, nullptr, Tracer::GetSerializer<sym::InvalidValueSerializer>()));
      return results;
    }
    auto indirectValueObject = std::make_shared<EagerMemoryContentsObject>(
      adjusted_address, adjusted_address + memory.value->Size(), std::move(memory.value));

    auto clonedContext = derefType->IsPrimitive() ? VariableContext::CloneFrom(0, context)
                                                  : Tracer::CloneFromVariableContext(context);

    if (derefType->IsArrayType()) {
      results.push_back(Ref<Value>::MakeShared(clonedContext,
        *derefType.TypeEntry(),
        0U,
        indirectValueObject,
        Tracer::GetSerializer<sym::ArraySerializer>()));
    } else if (derefType->IsPrimitive() || derefType->IsReference()) {
      results.push_back(Ref<Value>::MakeShared(clonedContext,
        *derefType.TypeEntry(),
        0U,
        indirectValueObject,
        Tracer::GetSerializer<sym::PrimitiveSerializer>()));
    } else {
      results.push_back(Ref<Value>::MakeShared(clonedContext,
        *derefType.TypeEntry(),
        0U,
        indirectValueObject,
        Tracer::GetSerializer<sym::DefaultStructSerializer>()));
    }
    ObjectFile::SetSerializerFor(*results.back());
    if (clonedContext->mId > 0) {
      clonedContext->mTask->CacheValueObject(clonedContext->mId, results.back());
    }
  }
  return results;
}

std::vector<Ref<Value>>
ResolveCString::Resolve(const VariableContext &context, ValueRange valueRange) noexcept
{
  std::vector<Ref<Value>> results;
  auto &value = *context.GetValue();
  if (const auto address = value.ToRemotePointer(); address.has_value()) {
    auto adjustedAddress = address.value() + (valueRange.start.value_or(0) * value.GetType()->Size());
    const auto requestedLength = valueRange.count.value_or(256);
    auto referencedMemory =
      sym::MemoryContentsObject::ReadMemory(*context.mTask->GetSupervisor(), adjustedAddress, requestedLength);
    CanonicalType layoutType = value.GetType().DerefType();
    if (!referencedMemory.is_ok()) {
      results.push_back(Ref<Value>::MakeShared(
        nullptr, *layoutType, 0U, nullptr, Tracer::GetSerializer<InvalidValueSerializer>()));
      return results;
    }
    auto indirectValueObject = std::make_shared<EagerMemoryContentsObject>(
      adjustedAddress, adjustedAddress + referencedMemory.value->Size(), std::move(referencedMemory.value));

    auto stringValue = Ref<sym::Value>::MakeShared(VariableContext::CloneFrom(0, context),
      *layoutType,
      0U,
      indirectValueObject,
      Tracer::GetSerializer<CStringSerializer>());

    results.push_back(std::move(stringValue));
  }
  return results;
}

std::vector<Ref<Value>>
ResolveArray::Resolve(const VariableContext &context, ValueRange valueRange) noexcept
{
  auto &value = *context.GetValue();
  CanonicalType type = value.GetType();
  MDB_ASSERT(type->IsArrayType(), "Expected value-type to be an array-type");
  std::vector<Ref<Value>> results;
  const auto arraySize = type->ArraySize();
  CanonicalType elementsType = type.DerefType();

  if (valueRange.start.value() >= arraySize) {
    return {};
  }

  auto count = std::min(valueRange.count.value_or(100), arraySize - valueRange.start.value());

  const u32 startIndex = valueRange.start.value();
  const u32 endIndex = startIndex + count;

  const auto arrayBaseAddress = value.Address();

  const auto desiredFirstElementAddress = arrayBaseAddress + (startIndex * elementsType->size_of);
  const u32 elementTypeSize = elementsType->size_of;
  auto underlying = (endIndex - startIndex) * elementTypeSize;
  // We make backing memory view for the entire sub-range.
  auto lazy = std::make_shared<LazyMemoryContentsObject>(
    *context.mTask->GetSupervisor(), desiredFirstElementAddress, desiredFirstElementAddress + underlying);

  for (auto i = 0U; i < (endIndex - startIndex); ++i) {
    const auto memoryObjectOffset = i * elementTypeSize;
    auto varContext = elementsType->IsPrimitive() ? VariableContext::CloneFrom(0, context)
                                                  : Tracer::CloneFromVariableContext(context);
    results.emplace_back(
      Ref<Value>::MakeShared(varContext, std::to_string(startIndex + i), *elementsType, memoryObjectOffset, lazy));

    ObjectFile::SetSerializerFor(*results.back());
    if (varContext->mId > 0) {
      context.mTask->CacheValueObject(varContext->mId, results.back());
    }
  }

  return results;
}

std::vector<Ref<Value>>
ResolveRange::Resolve(const VariableContext &context, ValueRange valueRange) noexcept
{
  TODO(__PRETTY_FUNCTION__);
  (void)context;
  (void)valueRange;
}

/* static */
std::optional<std::pmr::string>
PrimitiveSerializer::FormatEnum(Type &t, std::span<const u8> span, std::pmr::memory_resource *allocator) noexcept
{
  const EnumeratorValues &enums = t.GetEnumerations();
  EnumeratorConstValue value;
  if (enums.mIsSigned) {
    // NOLINTNEXTLINE
    switch (t.size_of) {
    case 1:
      value.i = BitCopy<i8>(span);
      break;
    case 2:
      value.i = BitCopy<i16>(span);
      break;
    case 4:
      value.i = BitCopy<i32>(span);
      break;
    case 8:
      value.i = BitCopy<i64>(span);
      break;
    }
  } else {
    switch (t.size_of) {
    case 1:
      value.u = BitCopy<u8>(span);
      break;
    case 2:
      value.u = BitCopy<u16>(span);
      break;
    case 4:
      value.u = BitCopy<u32>(span);
      break;
    case 8:
      value.u = BitCopy<u64>(span);
      break;
    }
  }

  const auto &fields = t.MemberFields();
  if (enums.mIsSigned) {
    for (auto i = 0U; i < fields.size(); ++i) {
      if (enums.mEnumeratorValues[i].i == value.i) {
        std::pmr::string result{ allocator };
        FormatAndReturn(result, "{}::{}", t.mName, fields[i].mName);
      }
    }
    std::pmr::string result{ allocator };
    FormatAndReturn(result, "{}::(invalid){}", t.mName, value.i);
  }
  for (auto i = 0U; i < fields.size(); ++i) {
    if (enums.mEnumeratorValues[i].u == value.u) {
      std::pmr::string result{ allocator };
      FormatAndReturn(result, "{}::{}", t.mName, fields[i].mName);
    }
  }
  std::pmr::string result{ allocator };
  FormatAndReturn(result, "{}::(invalid){}", t.mName, value.u);
}

template <typename To>
To
BitCopyAndMaybeProcessBitField(std::span<const u8> from, const Value &value)
{
  static_assert(std::is_trivial_v<To>, "Target of bit copy must be trivially constructible.");

  // Non-bitfield values is returned as-is.
  const auto bitField = value.BitField();
  return bitField
    .transform([&](const BitField &field) {
      u64 rawBits = 0;
      std::memcpy(&rawBits, from.data(), std::min(sizeof(u64), from.size()));
      return static_cast<To>(field.ExtractBits(rawBits));
    })
    .or_else([&]() -> std::optional<To> {
      To result{};
      std::memcpy(&result, from.data(), std::min(sizeof(To), from.size()));
      return result;
    })
    .value();
}

// TODO(simon): add optimization where we can format our value directly to an outbuf?
/* static */
std::optional<std::pmr::string>
PrimitiveSerializer::FormatValue(const Value &valueObject, std::pmr::memory_resource *allocator) noexcept
{
  const auto span = valueObject.MemoryView();
  if (span.empty()) {
    return std::nullopt;
  }

  CanonicalType type = valueObject.GetType();
  const auto size_of = type->size_of;

  std::pmr::string result{ allocator };

  if (type->IsReference()) {
    const auto ptr = BitCopy<std::uintptr_t>(span);
    FormatAndReturn(result, "0x{:x}", ptr);
  }

  if (type->GetDwarfTag() == DwarfTag::DW_TAG_enumeration_type) {
    return FormatEnum(*type, span, allocator);
  }

  switch (type->GetBaseType().value()) {
  case BaseTypeEncoding::DW_ATE_address: {
    auto value = BitCopyAndMaybeProcessBitField<std::uintptr_t>(span, valueObject);
    FormatAndReturn(result, "0x{}", value);
  }
  case BaseTypeEncoding::DW_ATE_boolean: {
    bool value = BitCopyAndMaybeProcessBitField<bool>(span, valueObject);
    FormatAndReturn(result, "{}", value);
  }
  case BaseTypeEncoding::DW_ATE_float: {
    if (size_of == 4U) {
      auto value = BitCopy<float>(span);
      FormatAndReturn(result, "{}", value);
    }
    if (size_of == 8U) {
      auto value = BitCopy<double>(span);
      FormatAndReturn(result, "{}", value);
    }
    PANIC("Expected byte size of a floating point to be 4 or 8");
  }
  case BaseTypeEncoding::DW_ATE_signed_char:
  case BaseTypeEncoding::DW_ATE_signed:
    // NOLINTNEXTLINE
    switch (size_of) {
    case 1: {
      auto value = BitCopyAndMaybeProcessBitField<signed char>(span, valueObject);
      FormatAndReturn(result, "{}", value);
    }
    case 2: {
      auto value = BitCopyAndMaybeProcessBitField<signed short>(span, valueObject);
      FormatAndReturn(result, "{}", value);
    }
    case 4: {
      int value = BitCopyAndMaybeProcessBitField<int>(span, valueObject);
      FormatAndReturn(result, "{}", value);
    }
    case 8: {
      auto value = BitCopyAndMaybeProcessBitField<signed long long>(span, valueObject);
      FormatAndReturn(result, "{}", value);
    }
    }
    break;
  case BaseTypeEncoding::DW_ATE_unsigned_char:
  case BaseTypeEncoding::DW_ATE_unsigned:
    // NOLINTNEXTLINE
    switch (size_of) {
    case 1: {
      u8 value = BitCopyAndMaybeProcessBitField<unsigned char>(span, valueObject);
      FormatAndReturn(result, "{}", value);
    }
    case 2: {
      u16 value = BitCopyAndMaybeProcessBitField<unsigned short>(span, valueObject);
      FormatAndReturn(result, "{}", value);
    }
    case 4: {
      u32 value = BitCopyAndMaybeProcessBitField<u32>(span, valueObject);
      FormatAndReturn(result, "{}", value);
    }
    case 8: {
      u64 value = BitCopyAndMaybeProcessBitField<u64>(span, valueObject);
      FormatAndReturn(result, "{}", value);
    }
    }
    break;
  case BaseTypeEncoding::DW_ATE_UTF: {
    u32 value = BitCopyAndMaybeProcessBitField<u32>(span, valueObject);
    FormatAndReturn(result, "{}", value);
  } break;
  case BaseTypeEncoding::DW_ATE_ASCII:
  case BaseTypeEncoding::DW_ATE_edited:
  case BaseTypeEncoding::DW_ATE_signed_fixed:
  case BaseTypeEncoding::DW_ATE_unsigned_fixed:
  case BaseTypeEncoding::DW_ATE_decimal_float:
  case BaseTypeEncoding::DW_ATE_imaginary_float:
  case BaseTypeEncoding::DW_ATE_packed_decimal:
  case BaseTypeEncoding::DW_ATE_numeric_string:
  case BaseTypeEncoding::DW_ATE_complex_float:

  case BaseTypeEncoding::DW_ATE_UCS: {
    TODO_FMT("Currently not implemented base type encoding: {}", to_str(type->GetBaseType().value()));
    break;
  }
  case BaseTypeEncoding::DW_ATE_lo_user:
  case BaseTypeEncoding::DW_ATE_hi_user:
    break;
  }
  PANIC("unknown base type encoding");
}

std::optional<std::pmr::string>
PrimitiveSerializer::SerializeVariable(const Value &value, std::pmr::memory_resource *allocator) noexcept
{
  const auto byte_span = value.MemoryView();
  if (byte_span.empty()) {
    return std::nullopt;
  }

  auto value_field =
    FormatValue(value, allocator).value_or(std::pmr::string{ "could not serialize value", allocator });
  std::pmr::string result{ allocator };

  FormatAndReturn(result,
    R"({{ "name": "{}", "value": "{}", "type": "{}", "variablesReference": {}, "memoryReference": "{}" }})",
    value.mName,
    value_field,
    *(value.GetType()),
    value.ReferenceId(),
    value.Address());
}

std::optional<std::pmr::string>
DefaultStructSerializer::SerializeVariable(const Value &value, std::pmr::memory_resource *allocator) noexcept
{

  const auto &t = *value.GetType();
  std::pmr::string result{ allocator };
  FormatAndReturn(result,
    R"({{ "name": "{}", "value": "{}", "type": "{}", "variablesReference": {}, "memoryReference": "{}" }})",
    value.mName,
    t,
    t,
    value.ReferenceId(),
    value.Address());
}

std::optional<std::pmr::string>
InvalidValueSerializer::SerializeVariable(const Value &value, std::pmr::memory_resource *allocator) noexcept
{
  std::pmr::string result{ allocator };
  FormatAndReturn(result,
    R"({{ "name": "{}", "value": "could not resolve {}", "type": "{}", "variablesReference": 0 }})",
    value.mName,
    value.mName,
    *value.GetType());
}

std::optional<std::pmr::string>
ArraySerializer::SerializeVariable(const Value &value, std::pmr::memory_resource *allocator) noexcept
{
  auto &t = *value.GetType();
  const Type *no_alias = t.ResolveAlias();
  std::pmr::string result{ allocator };
  FormatAndReturn(result,
    R"({{ "name": "{}", "value": "{}", "type": "{}", "variablesReference": {}, "memoryReference": "{}", "indexedVariables": {} }})",
    value.mName,
    t,
    t,
    value.ReferenceId(),
    value.Address(),
    no_alias->ArraySize());
}

std::optional<std::pmr::string>
CStringSerializer::SerializeVariable(const Value &value, std::pmr::memory_resource *allocator) noexcept
{
  const auto byteSpan = value.FullMemoryView();

  if (byteSpan.empty()) {
    return std::nullopt;
  }

  auto type = value.GetType();
  const auto baseEncoding = type->GetBaseType();
  const u32 elemSize = type->size_of;

  const bool isUtf16 =
    elemSize == 2 && baseEncoding.has_value() &&
    (baseEncoding.value() == BaseTypeEncoding::DW_ATE_UCS || baseEncoding.value() == BaseTypeEncoding::DW_ATE_UTF);

  std::pmr::string result{ allocator };

  std::optional<u32> null_terminator = {};
  const auto size = byteSpan.size_bytes();
  for (auto i = 0U; i < size; ++i) {
    if (byteSpan[i] == 0) {
      null_terminator = i;
      break;
    }
  }

  std::string_view cast{ (const char *)byteSpan.data(), null_terminator.value_or(byteSpan.size_bytes()) };
  FormatAndReturn(result,
    R"({{ "name": "{}", "value": "{}", "type": "const char *", "variablesReference": {}, "memoryReference": "{}" }})",
    value.mName,
    DAPStringView{ cast },
    0,
    value.Address());
}

#undef FormatAndReturn

// TODO(simon): add optimization where we can format our value directly to an outbuf?

#define FormatAndReturn(result, formatString, ...)                                                                \
  return std::format_to(result, formatString __VA_OPT__(, ) __VA_ARGS__);

template <typename Iterator>
static Iterator
FormatEnum(Type &t, std::span<const u8> span, Iterator &result) noexcept
{
  const EnumeratorValues &enums = t.GetEnumerations();
  EnumeratorConstValue value;
  if (enums.mIsSigned) {
    // NOLINTNEXTLINE
    switch (t.size_of) {
    case 1:
      value.i = BitCopy<i8>(span);
      break;
    case 2:
      value.i = BitCopy<i16>(span);
      break;
    case 4:
      value.i = BitCopy<i32>(span);
      break;
    case 8:
      value.i = BitCopy<i64>(span);
      break;
    }
  } else {
    // NOLINTNEXTLINE
    switch (t.size_of) {
    case 1:
      value.u = BitCopy<u8>(span);
      break;
    case 2:
      value.u = BitCopy<u16>(span);
      break;
    case 4:
      value.u = BitCopy<u32>(span);
      break;
    case 8:
      value.u = BitCopy<u64>(span);
      break;
    }
  }

  const auto &fields = t.MemberFields();
  if (enums.mIsSigned) {
    for (auto i = 0U; i < fields.size(); ++i) {
      if (enums.mEnumeratorValues[i].i == value.i) {
        FormatAndReturn(result, "{}::{}", t.mName, fields[i].mName);
      }
    }
    FormatAndReturn(result, "{}::(invalid){}", t.mName, value.i);
  }
  for (auto i = 0U; i < fields.size(); ++i) {
    if (enums.mEnumeratorValues[i].u == value.u) {
      FormatAndReturn(result, "{}::{}", t.mName, fields[i].mName);
    }
  }
  FormatAndReturn(result, "{}::(invalid){}", t.mName, value.u);
}

template <typename Iterator>
Iterator
FormatValue(Value &value, Iterator iter) noexcept
{
  const auto span = value.MemoryView();
  if (span.empty()) {
    return std::format_to(iter, "<err:value had no memory contents>");
  }
  CanonicalType type = value.GetType();
  const auto size_of = type->SizeBytes();

  if (type->IsReference()) {
    const auto ptrValue = BitCopy<std::uintptr_t>(span);
    FormatAndReturn(iter, "0x{:x}", ptrValue);
  }

  if (type->GetDwarfTag() == DwarfTag::DW_TAG_enumeration_type) {
    return FormatEnum(*type, span, iter);
  }

  switch (type->GetBaseType().value()) {
  case BaseTypeEncoding::DW_ATE_address: {
    std::uintptr_t value = BitCopy<std::uintptr_t>(span);
    FormatAndReturn(iter, "0x{}", value);
  }
  case BaseTypeEncoding::DW_ATE_boolean: {
    bool value = BitCopy<bool>(span);
    FormatAndReturn(iter, "{}", value);
  }
  case BaseTypeEncoding::DW_ATE_float: {
    if (size_of == 4U) {
      auto value = BitCopy<float>(span);
      FormatAndReturn(iter, "{}", value);
    }
    if (size_of == 8U) {
      auto value = BitCopy<double>(span);
      FormatAndReturn(iter, "{}", value);
    }
    PANIC("Expected byte size of a floating point to be 4 or 8");
  }
  case BaseTypeEncoding::DW_ATE_signed_char:
  case BaseTypeEncoding::DW_ATE_signed:
    // NOLINTNEXTLINE
    switch (size_of) {
    case 1: {
      auto value = BitCopy<signed char>(span);
      FormatAndReturn(iter, "{}", value);
    }
    case 2: {
      auto value = BitCopy<signed short>(span);
      FormatAndReturn(iter, "{}", value);
    }
    case 4: {
      int value = BitCopy<int>(span);
      FormatAndReturn(iter, "{}", value);
    }
    case 8: {
      auto value = BitCopy<signed long long>(span);
      FormatAndReturn(iter, "{}", value);
    }
    }
    break;
  case BaseTypeEncoding::DW_ATE_unsigned_char:
  case BaseTypeEncoding::DW_ATE_unsigned:
    // NOLINTNEXTLINE
    switch (size_of) {
    case 1: {
      u8 value = BitCopy<unsigned char>(span);
      FormatAndReturn(iter, "{}", value);
    }
    case 2: {
      u16 value = BitCopy<unsigned short>(span);
      FormatAndReturn(iter, "{}", value);
    }
    case 4: {
      u32 value = BitCopy<u32>(span);
      FormatAndReturn(iter, "{}", value);
    }
    case 8: {
      u64 value = BitCopy<u64>(span);
      FormatAndReturn(iter, "{}", value);
    }
    }
    break;
  case BaseTypeEncoding::DW_ATE_UTF: {
    u32 value = BitCopy<u32>(span);
    FormatAndReturn(iter, "{}", value);
  } break;
  case BaseTypeEncoding::DW_ATE_ASCII:
  case BaseTypeEncoding::DW_ATE_edited:
  case BaseTypeEncoding::DW_ATE_signed_fixed:
  case BaseTypeEncoding::DW_ATE_unsigned_fixed:
  case BaseTypeEncoding::DW_ATE_decimal_float:
  case BaseTypeEncoding::DW_ATE_imaginary_float:
  case BaseTypeEncoding::DW_ATE_packed_decimal:
  case BaseTypeEncoding::DW_ATE_numeric_string:
  case BaseTypeEncoding::DW_ATE_complex_float:

  case BaseTypeEncoding::DW_ATE_UCS: {
    TODO_FMT("Currently not implemented base type encoding: {}", to_str(type->GetBaseType().value()));
    break;
  }
  case BaseTypeEncoding::DW_ATE_lo_user:
  case BaseTypeEncoding::DW_ATE_hi_user:
    break;
  }
  PANIC("unknown base type encoding");
}

template <size_t N>
static consteval StringLiteral<N>
IndentString() noexcept
{
  StringLiteral<N> buf;
  for (auto &c : buf.mValue) {
    c = ' ';
  }
  return StringLiteral<N>{ buf };
}

static constexpr std::array<char, 257> IndentStringArray{
  "                                                                                                               "
  "                                                                                                               "
  "                                  "
};

static constexpr std::string_view
GetIndent(uint64_t level) noexcept
{
  return std::string_view{ IndentStringArray.data(), level * 2 };
}

/* static */
template <typename FmtIterator>
FmtIterator
JavascriptValueSerializer::Serialize(
  Value *value, FmtIterator fmtIterator, const SerializeOptions &options, int currentDepth) noexcept
{
  static constexpr auto finalizeField = [](auto it, const auto &opts) noexcept {
    if (opts.mNewLineAfterMember) {
      return std::format_to(it, ",\n");
    }
    return std::format_to(it, ", ");
  };

  CanonicalType valueType = value->GetType();

  auto indentLevel = options.mDepth - currentDepth;
  auto indent = GetIndent(options.mNewLineAfterMember ? indentLevel : 0);

  if (valueType->IsPrimitive()) {
    auto it = std::format_to(fmtIterator, "{}{} : ", indent, value->mName);
    it = FormatValue(*value, it);
    return finalizeField(it, options);
  }
  if (currentDepth == 0) {
    auto it = std::format_to(fmtIterator, "{}{} : struct {}{{ .. }}", indent, value->mName, valueType.Name());
    return finalizeField(it, options);
  }
  // This is a struct/class
  auto it = std::format_to(fmtIterator, "{}{} : {}{{", indent, value->mName, valueType.Name());
  if (options.mNewLineAfterMember) {
    it = std::format_to(it, "\n");
  }

  for (const auto &m : valueType->MemberFields()) {
    auto v = value->GetMember(m.mName);
    it = Serialize(v, it, options, currentDepth - 1);
  }
  std::format_to(it, "{}}}", indent);
  return finalizeField(it, options);
}

/* static */
template <typename StringType>
bool
JavascriptValueSerializer::Serialize(
  Value *value, StringType &outputBuffer, const SerializeOptions &options) noexcept
{
  auto it = std::back_inserter(outputBuffer);
  CanonicalType valueType = value->GetType();

  if (valueType->IsPrimitive()) {
    FormatValue(*value, it);
    return true;
  }

  if (options.mNewLineAfterMember) {
    it = std::format_to(it, "{{\n");
  } else {
    it = std::format_to(it, "{{ ");
  }

  for (const auto &m : valueType->MemberFields()) {
    auto v = value->GetMember(m.mName);
    it = Serialize(v, it, options, options.mDepth - 1);
  }
  std::format_to(std::back_inserter(outputBuffer), "}}");

  return true;
}

template bool JavascriptValueSerializer::Serialize(
  Value *value, std::string &fmtIterator, const SerializeOptions &options) noexcept;

template bool JavascriptValueSerializer::Serialize(
  Value *value, std::pmr::string &fmtIterator, const SerializeOptions &options) noexcept;

} // namespace mdb::sym