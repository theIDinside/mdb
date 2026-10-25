/** LICENSE TEMPLATE */
#include "value.h"

// mdb
#include <common.h>
#include <interface/dap/types.h>
#include <interface/tracee_command/supervisor_state.h>
#include <lib/arena_allocator.h>
#include <symbolication/dwarf/die.h>
#include <symbolication/dwarf/typeread.h>
#include <symbolication/dwarf_expressions.h>
#include <symbolication/objfile.h>
#include <symbolication/type.h>
#include <symbolication/value_visualizer.h>
#include <task.h>
#include <tracer.h>
#include <utils/todo.h>

// std
#include <memory_resource>
#include <type_traits>

namespace mdb::sym {

void
Value::SetKind(Symbol *symbol) noexcept
{
  kind = ValueKind::Symbol;
  uSymbol = symbol;
}

void
Value::SetKind(Field *field) noexcept
{
  kind = ValueKind::Field;
  uField = field;
}

void
Value::SetKind(Type *type) noexcept
{
  kind = ValueKind::AbsoluteAddress;
  uType = type;
}

void
Value::SetKind(SyntheticType type) noexcept
{
  kind = ValueKind::SyntheticArray;
  uSynthetic = type;
}

void
Value::SetDapSerializer(DebugAdapterSerializer *serializer) noexcept
{
  mVisualizer = serializer;
}

bool
Value::ValueIsBitField() const noexcept
{
  if (kind != ValueKind::Field) {
    return false;
  }

  return !(uField->mBitFieldSize == 0 && uField->mFieldOffset == 0);
}

std::optional<BitField>
Value::BitField() const noexcept
{
  if (ValueIsBitField()) {
    return uField->GetBitField();
  }
  return {};
}

Value::Value(VarContext context,
  std::string_view name,
  Symbol &kind,
  u32 memContentsOffset,
  std::shared_ptr<MemoryContentsObject> &&valueObject,
  DebugAdapterSerializer *serializer) noexcept
    : mName(name), mMemoryContentsOffsets(memContentsOffset), mValueObject(std::move(valueObject)),
      mVisualizer(serializer), mContext(std::move(context)), mCanonicalType(kind.Type())
{
  SetKind(&kind);
}

Value::Value(VarContext context,
  std::string_view memberName,
  Field &kind,
  u32 containingStructureOffset,
  std::shared_ptr<MemoryContentsObject> valueObject,
  DebugAdapterSerializer *serializer) noexcept
    : mName(memberName), mMemoryContentsOffsets(containingStructureOffset + kind.mObjectBaseOffset),
      mValueObject(std::move(valueObject)), mVisualizer(serializer), mContext(std::move(context)),
      mCanonicalType(CanonicalType::From(kind.mType))
{
  SetKind(&kind);
}

Value::Value(VarContext context,
  Type &type,
  u32 memContentsOffset,
  std::shared_ptr<MemoryContentsObject> valueObject,
  DebugAdapterSerializer *serializer) noexcept
    : mName("value"), mMemoryContentsOffsets(memContentsOffset), mValueObject(std::move(valueObject)),
      mVisualizer(serializer), mContext(std::move(context)), mCanonicalType(CanonicalType::From(type))
{
  SetKind(&type);
}

Value::Value(VarContext context,
  std::string &&name,
  Type &type,
  u32 memContentsOffset,
  std::shared_ptr<MemoryContentsObject> valueObject,
  DebugAdapterSerializer *serializer) noexcept
    : mName(std::move(name)), mMemoryContentsOffsets(memContentsOffset), mValueObject(std::move(valueObject)),
      mVisualizer(serializer), mContext(std::move(context)), mCanonicalType(CanonicalType::From(type))
{
  SetKind(&type);
}

Value::Value(VarContext context,
  std::string name,
  SyntheticType type,
  u32 memContentsOffset,
  std::shared_ptr<MemoryContentsObject> valueObject) noexcept
    : mName(std::move(name)), mMemoryContentsOffsets(memContentsOffset), mValueObject(std::move(valueObject)),
      mContext(std::move(context)), mCanonicalType(CanonicalType::From(NonNull(*type.mLayoutType)))
{
  SetKind(type);
}

Value::~Value() noexcept { DBGLOG(dap, "Destroying value {}", mName); }

AddrPtr
Value::Address() const noexcept
{
  const auto result = mValueObject->start + mMemoryContentsOffsets;
  return result;
}

bool
Value::HasTargetAddress() const noexcept
{
  return mValueObject != nullptr && mValueObject->HasTargetAddress();
}

CanonicalType
Value::GetType() const noexcept
{
  return mCanonicalType;
}

std::expected<AddrPtr, ValueError>
Value::ToRemotePointer() const noexcept
{
  const auto bytes = MemoryView();
  if (bytes.size_bytes() != 8) {
    return ValueError::InvalidSize(bytes.size_bytes());
  }

  if (mCanonicalType->IsReference()) {
    std::uintptr_t ptr{};
    std::memcpy(&ptr, bytes.data(), 8);
    return AddrPtr{ ptr };
  }

  if (IsSynthetic()) {
    return Address();
  }

  return ValueError::NotAReference();
}

std::expected<std::vector<Ref<Value>>, ValueError>
Value::Dereference(u32 count) const noexcept
{
  if (!mContext) {
    return ValueError::NoVariableContext();
  }

  auto memoryObjectResult = DereferenceMemoryContentsObject(mContext->mTask->GetSupervisor(), count);
  if (!memoryObjectResult.has_value()) {
    return std::unexpected(memoryObjectResult.error());
  }

  std::vector<Ref<Value>> results;
  results.reserve(count);

  // Get the pointer value from this reference
  CanonicalType derefType = GetType().DerefType();
  const u64 sizePerElement = derefType->SizeBytes();

  const auto &memoryObject = *memoryObjectResult;
  for (u32 index = 0; index < count; ++index) {
    auto variableContext = derefType->IsPrimitive() ? VariableContext::CloneFrom(0, *mContext)
                                                    : Tracer::CloneFromVariableContext(*mContext);
    const u32 offset = static_cast<u32>(index * sizePerElement);

    auto elementValue =
      Ref<Value>::MakeShared(variableContext, std::format("[{}]", index), *derefType, offset, memoryObject);

    ObjectFile::SetSerializerFor(*elementValue);

    if (variableContext->mId > 0) {
      variableContext->mTask->CacheValueObject(variableContext->mId, elementValue);
    }

    results.push_back(std::move(elementValue));
  }

  return results;
}

std::expected<u32, ValueError>
Value::Dereference(u32 count, const std::function<bool(Ref<Value> value)> &onEachValue) const noexcept
{
  if (!mContext) {
    return ValueError::NoVariableContext();
  }

  auto memoryObject = DereferenceMemoryContentsObject(mContext->mTask->GetSupervisor(), count);
  if (!memoryObject.has_value()) {
    return std::unexpected(memoryObject.error());
  }

  std::vector<Ref<Value>> results;
  results.reserve(count);

  // Get the pointer value from this reference
  CanonicalType derefType = mCanonicalType.DerefType();
  const u64 sizePerElement = derefType->Size();

  u32 index = 0;
  for (; index < count; ++index) {
    auto variableContext = derefType->IsPrimitive() ? VariableContext::CloneFrom(0, *mContext)
                                                    : Tracer::CloneFromVariableContext(*mContext);
    const u32 offset = static_cast<u32>(index * sizePerElement);

    auto elementValue =
      Ref<Value>::MakeShared(variableContext, std::format("[{}]", index), *derefType, offset, *memoryObject);

    ObjectFile::SetSerializerFor(*elementValue);

    if (variableContext->mId > 0) {
      variableContext->mTask->CacheValueObject(variableContext->mId, elementValue);
    }

    onEachValue(std::move(elementValue));
  }

  return index;
}

bool
Value::HasVisualizer() const noexcept
{
  return mVisualizer != nullptr;
}

bool
Value::IsValidValue() const noexcept
{
  if (mValueObject == nullptr) {
    return false;
  }

  return !mValueObject->RawView().empty();
}

bool
Value::HasMember(std::string_view memberName) const noexcept
{
  CanonicalType type = GetType();
  return std::ranges::any_of(
    type->MemberFields(), [&memberName](const Field &field) { return field.mName == memberName; });
}

Ref<Value>
Value::GetMember(std::string_view memberName) noexcept
{
  if (!mContext) {
    return nullptr;
  }

  for (const Field &member : mCanonicalType->GetMemberFields()) {
    if (member.mName == memberName) {
      MDB_ASSERT(mContext, "Creating member from value that has no context");
      auto variableContext = member.mType->IsPrimitive() ? VariableContext::CloneFrom(0, *mContext)
                                                         : Tracer::CloneFromVariableContext(*mContext);
      const auto vId = variableContext->mId;
      auto memberValue = Ref<sym::Value>::MakeShared(variableContext,
        member.mName,
        const_cast<sym::Field &>(member),
        mMemoryContentsOffsets,
        TakeMemoryReference());
      ObjectFile::SetSerializerFor(*memberValue);

      if (vId > 0) {
        variableContext->mTask->CacheValueObject(vId, memberValue);
      }
      return memberValue;
    }
  }
  return nullptr;
}

size_t
Value::PushMemberValue(std::function<bool(Ref<Value> value)> &&onEachValue)
{
  if (!mContext) {
    return 0;
  }

  if (IsSynthetic()) {
    for (size_t i = 0; i < uSynthetic.mCount; ++i) {
      auto variableContext = mCanonicalType->IsPrimitive() ? VariableContext::CloneFrom(0, *mContext)
                                                           : Tracer::CloneFromVariableContext(*mContext);
      const u32 elementOffset = static_cast<u32>(i * mCanonicalType->SizeBytes());
      Ref<Value> elementValue = Ref<sym::Value>::MakeShared(
        variableContext, std::format("{}", i), *mCanonicalType, elementOffset, TakeMemoryReference());
      ObjectFile::SetSerializerFor(*elementValue);
      if (variableContext->mId > 0) {
        variableContext->mTask->CacheValueObject(variableContext->mId, elementValue);
      }
      onEachValue(std::move(elementValue));
    }
    return uSynthetic.mCount;
  }

  size_t count = 0;
  for (const Field &member : mCanonicalType->MemberFields()) {
    MDB_ASSERT(mContext, "Creating member from value that has no context");
    auto variableContext = member.mType->IsPrimitive() ? VariableContext::CloneFrom(0, *mContext)
                                                       : Tracer::CloneFromVariableContext(*mContext);
    const auto vId = variableContext->mId;
    auto memberValue = Ref<sym::Value>::MakeShared(variableContext,
      member.mName,
      const_cast<sym::Field &>(member),
      mMemoryContentsOffsets,
      TakeMemoryReference());
    ObjectFile::SetSerializerFor(*memberValue);

    if (vId > 0) {
      variableContext->mTask->CacheValueObject(vId, memberValue);
    }
    onEachValue(std::move(memberValue));
    ++count;
  }
  return count;
}

VariableReferenceId
Value::ReferenceId() const noexcept
{
  // This value is invalid (for whatever reason).
  if (!mContext) {
    return 0;
  }
  return mContext->mId;
}

bool
Value::IsLive() const noexcept
{
  return mContext->IsLiveReference();
}

void
Value::RegisterContext() noexcept
{
  Tracer::SetVariableContext(mContext);
  mContext->mTask->CacheValueObject(mContext->mId, RefPtr<sym::Value>{ this });
}

bool
Value::OverwriteValueBytes(std::span<const std::byte> newBytes) noexcept
{
  auto oldSpan = MemoryView();
  if (newBytes.size() > oldSpan.size()) {
    return false;
  }
  auto addr = Address();

  auto supervisor = mContext->mTask->GetSupervisor();
  const auto result = supervisor->DoWriteBytes(addr, (const u8 *)newBytes.data(), newBytes.size());
  mValueObject->Refresh(*supervisor);

  return result.mWasSuccessful;
}

template <class T>
static constexpr auto
ByteViewOf(const T &t) -> std::span<const std::byte>
{
  return std::as_bytes(std::span<const T>{ std::addressof(t), 1 });
}

[[nodiscard]] VarContext
Value::GetVariableContext() const
{
  return mContext;
}

template <typename Primitive>
bool
Value::WritePrimitive(Primitive value) noexcept
{
  CanonicalType type = GetType();

  if (type->IsReference()) {
    if constexpr (!std::is_integral_v<Primitive>) {
      return false;
    }
    auto cast = static_cast<u64>(value);
    return OverwriteValueBytes(ByteViewOf(cast));
  }

  // if *not* a reference, or *is* array type, get layout describing type
  MDB_ASSERT(!GetType()->IsArrayType(),
    "At some point in time, we supported type = type.DerefType() if type was array, but I don't remember why. It "
    "doesn't make sense to me anymore.");

  auto baseType = type->GetBaseType();
  // TODO(simon): implement support for enum, which can have base type underlying types (at which point baseType ==
  // nullopt). When that is fixed, the check below should be baseType->IsPrimitive(), which also lets enum types
  // (with primitive backing storage) through
  if (!baseType) {
    return false;
  }
  auto sz = type->SizeBytes();
  switch (baseType.value()) {
  case BaseTypeEncoding::DW_ATE_float: {
    if constexpr (!std::is_floating_point_v<Primitive>) {
      return false;
    }
    switch (sz) {
    case 4: {
      auto cast = static_cast<f32>(value);
      return OverwriteValueBytes(ByteViewOf(cast));
    } break;
    case 8: {
      auto cast = static_cast<f64>(value);
      return OverwriteValueBytes(ByteViewOf(cast));
    } break;
    }
  } break;
  case BaseTypeEncoding::DW_ATE_signed_char:
    [[fallthrough]];
  case BaseTypeEncoding::DW_ATE_signed: {
    if constexpr (!std::is_integral_v<Primitive>) {
      return false;
    }
    switch (sz) {
    case 1: {
      auto cast = static_cast<i8>(value);
      return OverwriteValueBytes(ByteViewOf(cast));
    } break;
    case 2: {
      auto cast = static_cast<i16>(value);
      return OverwriteValueBytes(ByteViewOf(cast));
    } break;
    case 4: {
      auto cast = static_cast<i32>(value);
      return OverwriteValueBytes(ByteViewOf(cast));
    } break;
    case 8: {
      auto cast = static_cast<i64>(value);
      return OverwriteValueBytes(ByteViewOf(cast));
    } break;
    }
  } break;
  case BaseTypeEncoding::DW_ATE_unsigned_char:
    [[fallthrough]];
  case BaseTypeEncoding::DW_ATE_unsigned: {
    if constexpr (!std::is_integral_v<Primitive>) {
      return false;
    }
    switch (sz) {
    case 1: {
      auto cast = static_cast<u8>(value);
      return OverwriteValueBytes(ByteViewOf(cast));
    } break;
    case 2: {
      auto cast = static_cast<u16>(value);
      return OverwriteValueBytes(ByteViewOf(cast));
    } break;
    case 4: {
      auto cast = static_cast<u32>(value);
      return OverwriteValueBytes(ByteViewOf(cast));
    } break;
    case 8: {
      auto cast = static_cast<u64>(value);
      return OverwriteValueBytes(ByteViewOf(cast));
    } break;
    }
  } break;

  default:
    break;
  }
  return false;
}

template bool Value::WritePrimitive<bool>(bool value) noexcept;
template bool Value::WritePrimitive<i8>(i8 value) noexcept;
template bool Value::WritePrimitive<i16>(i16 value) noexcept;
template bool Value::WritePrimitive<i32>(i32 value) noexcept;
template bool Value::WritePrimitive<i64>(i64 value) noexcept;

template bool Value::WritePrimitive<u8>(u8 value) noexcept;
template bool Value::WritePrimitive<u16>(u16 value) noexcept;
template bool Value::WritePrimitive<u32>(u32 value) noexcept;
template bool Value::WritePrimitive<u64>(u64 value) noexcept;

template bool Value::WritePrimitive<f32>(f32 value) noexcept;
template bool Value::WritePrimitive<f64>(f64 value) noexcept;

std::expected<std::shared_ptr<MemoryContentsObject>, ValueError>
Value::DereferenceMemoryContentsObject(tc::SupervisorState *supervisor, u32 count) const
{
  if (!mContext) {
    return ValueError::NoVariableContext();
  }
  // Get the pointer value from this reference
  auto pointerResult = ToRemotePointer();
  if (!pointerResult.has_value()) {
    return std::unexpected(pointerResult.error());
  }

  const auto derefType = mCanonicalType.DerefType();
  const u64 sizePerElement = derefType->Size();
  const u64 totalSize = sizePerElement * count;

  const AddrPtr address = *pointerResult;
  auto memory = MemoryContentsObject::ReadMemory(*supervisor, address, totalSize);

  // Check if memory read was successful
  if (!memory.is_ok()) {
    return ValueError::InvalidMemoryAddress(address);
  }

  return std::make_shared<EagerMemoryContentsObject>(address, address + totalSize, std::move(memory.value));
}

DebugAdapterSerializer *
Value::GetSerializer() noexcept
{
  return mVisualizer;
}

SharedPtr<MemoryContentsObject>
Value::TakeMemoryReference() noexcept
{
  return mValueObject;
}

std::span<const u8>
Value::MemoryView() const noexcept
{
  u32 size = 0;
  switch (kind) {
  case ValueKind::SyntheticArray:
    size = uSynthetic.Size();
    break;
  default:
    size = mCanonicalType->Size();
  }
  return mValueObject->View(mMemoryContentsOffsets, size);
}

std::span<const u8>
Value::FullMemoryView() const noexcept
{
  return mValueObject->RawView();
}

MemoryContentsObject::MemoryContentsObject(AddrPtr start, AddrPtr end) noexcept : start(start), end(end) {}

// A read of target memory may have been partial; never hand out a view that extends past the bytes we actually
// have. A view that can't be fully satisfied is empty, which serializers treat as "value unavailable".
static std::span<const u8>
ClampedSubspan(std::span<const u8> bytes, u32 offset, u32 size) noexcept
{
  if (static_cast<u64>(offset) + size > bytes.size()) {
    return {};
  }
  return bytes.subspan(offset, size);
}

EagerMemoryContentsObject::EagerMemoryContentsObject(
  AddrPtr start, AddrPtr end, MemoryContentBytes &&data) noexcept
    : MemoryContentsObject(start, end), mContents(std::move(data))
{
}

LazyMemoryContentsObject::LazyMemoryContentsObject(
  tc::SupervisorState &supervisor, AddrPtr start, AddrPtr end) noexcept
    : MemoryContentsObject(start, end), mSupervisor(supervisor)
{
}

bool
EagerMemoryContentsObject::Refresh(tc::SupervisorState &supervisor) noexcept
{
  auto mem = ReadMemory(supervisor, start, RawView().size_bytes());
  MDB_ASSERT(mem.is_ok(), "failed to refresh {} .. {}", start, end);
  mContents = std::move(mem.value);
  return mem.is_ok();
}

std::span<const u8>
EagerMemoryContentsObject::RawView() noexcept
{
  return mContents->Span();
}

std::span<const u8>
EagerMemoryContentsObject::View(u32 offset, u32 size) noexcept
{
  return ClampedSubspan(mContents->Span(), offset, size);
}

void
LazyMemoryContentsObject::CacheMemory() noexcept
{
  DBGLOG(dap, "[lazy transfer]: {} .. {}", start, end);
  if (auto res = mSupervisor.SafeRead(start, end->GetRaw() - start->GetRaw()); res.is_expected()) {
    mContents = std::move(res.take_value());
  } else {
    mContents = std::move(res.take_error().mBytes);
  }
}

bool
LazyMemoryContentsObject::Refresh(tc::SupervisorState &) noexcept
{
  CacheMemory();
  return true;
}

std::span<const u8>
LazyMemoryContentsObject::RawView() noexcept
{
  if (mContents == nullptr) {
    CacheMemory();
  }

  return mContents->Span();
}

std::span<const u8>
LazyMemoryContentsObject::View(u32 offset, u32 size) noexcept
{
  if (mContents == nullptr) {
    CacheMemory();
  }

  return ClampedSubspan(mContents->Span(), offset, size);
}

/*static*/
MemoryContentsObject::ReadResult
MemoryContentsObject::ReadMemory(tc::SupervisorState &tc, AddrPtr address, u64 sizeOf) noexcept
{
  auto res = tc.SafeRead(address, sizeOf);
  if (res.is_expected()) {
    return ReadResult{ .info = ReadResultInfo::Success, .value = res.take_value() };
  }
  const auto read_bytes = sizeOf - res.error().mUnreadBytes;
  if (read_bytes != 0) {
    return ReadResult{ .info = ReadResultInfo::Partial, .value = std::move(res.take_error().mBytes) };
  }
  return ReadResult{ .info = ReadResultInfo::Failed, .value = nullptr };
}

/*static*/
MemoryContentsObject::ReadResult
MemoryContentsObject::ReadMemory(std::pmr::memory_resource *, tc::SupervisorState &, AddrPtr, u32) noexcept
{
  TODO("implement MemoryContentsObject that uses custom allocation strategies.");
}

static void
ReadInLocationList(Symbol &symbol, alloc::ArenaResource *allocator, const ElfSection &locList) noexcept
{
  uint64_t base;
  uint64_t start;
  uint64_t end;

  std::span<const u8> loclist = locList.GetDataAs<const u8>().subspan(symbol.Location().LocListOffset());

  struct span
  {
    const u8 *ptr;
    u64 size;

    constexpr void
    Move(u32 offset)
    {
      ptr += offset;
      size -= offset;
    }

    inline void
    CopyTo(uint64_t &value) noexcept
    {
      std::memcpy(&value, ptr, 8);
      Move(8);
    }

    inline void
    CopyTo(uint16_t &value) noexcept
    {
      std::memcpy(&value, ptr, 2);
      Move(2);
    }
  };

  span s{ .ptr = loclist.data(), .size = loclist.size() };

  auto arena = allocator->ScopeAllocation();
  std::pmr::vector<LocationListEntry> parsed{ arena.GetAllocator() };
  parsed.reserve(512);
  std::vector<LocationListEntry> result;

  while (s.size >= 16U) {
    s.CopyTo(start);
    if (start == 0xFFFFFFFF'FFFFFFFF) {
      s.CopyTo(base);
      continue;
    }
    s.CopyTo(end);
    if (start == 0 && end == 0) {
      break;
    }
    u16 dwarfExpressionLength;
    s.CopyTo(dwarfExpressionLength);
    parsed.push_back(LocationListEntry{
      .mStart = start + base, .mEnd = end + base, .mDwarfExpression = { s.ptr, dwarfExpressionLength } });
    s.Move(dwarfExpressionLength);
  }
  result.reserve(parsed.size());
  std::ranges::copy(parsed, std::back_inserter(result));

  symbol.Location().SetLocationList(new LocationList{ std::move(result) });
}

/*static*/
Ref<Value>
MemoryContentsObject::CreateFrameVariable(
  tc::SupervisorState &tc, const sym::Frame &frame, Symbol &symbol, bool lazy) noexcept
{
  CanonicalType type = symbol.Type();
  const auto requestedByteSize = type->Size();

  auto *fnSymbol = frame.MaybeGetFullSymbolInfo();
  if (!fnSymbol) {
    DBGLOG(dap, "could not find function symbol for frame. Required to construct live variables.");
    TODO("Add support for situations where we can't actually construct the value");
    return nullptr;
  }

  if (!symbol.Computed()) {
    ReadInLocationList(symbol,
      tc.GetDebugAdapterProtocolClient()->GetCommandArenaAllocator(),
      *frame.GetSymbolFile()->GetObjectFile()->GetElf()->mDebugLoclist);
  }
  auto dwarfExpression = symbol.GetDwarfExpression(frame.GetSymbolFile()->UnrelocateAddress(frame.FramePc()));

  if (dwarfExpression.empty()) {
    DBGLOG(core, "DWARF expression to get location description is empty for {}", symbol.Name());
    return Ref<Value>::MakeShared(nullptr, symbol.Name(), symbol, u32{ 0 }, nullptr);
  }
  auto interp = ExprByteCodeInterpreter{ frame.FrameLevel(),
    tc,
    *frame.mTask,
    dwarfExpression,
    fnSymbol->GetFrameBaseDwarfExpression(),
    frame.GetSymbolFile()->GetObjectFile() };

  auto variableContext = type->IsPrimitive() ? VariableContext::CreateFromFrame(0, ContextType::Variable, frame)
                                             : VariableContext::CreateFromFrame(
                                                 Tracer::NewVariablesReference(), ContextType::Variable, frame);

  const auto location = interp.Run();

  // TODO: Handle composite locations properly by creating CompositeMemoryContentsObject
  // For now, we only support simple memory locations
  if (!location.IsSimple() || location.mKind != LocationKind::Memory) {
    DBGLOG(core,
      "Variable {} has non-simple location (composite/register/implicit) - not yet fully supported",
      symbol.Name());
    // For now, treat as address nullptr (will show as unavailable)
    const AddrPtr address = nullptr;
    auto memoryObject = std::make_shared<LazyMemoryContentsObject>(tc, address, address);
    return Ref<sym::Value>::MakeShared(
      std::move(variableContext), symbol.Name(), symbol, u32{ 0 }, std::move(memoryObject));
  }

  const AddrPtr address = AddrPtr{ location.uAddress };

  if (lazy) {
    DBGLOG(core,
      "creating lazy memory object for symbol {} of requested size {}, for type=0x{:x}",
      symbol.Name(),
      requestedByteSize,
      type->mCompUnitDieReference->ToDieReference().SectionOffset());
    auto memoryObject = std::make_shared<LazyMemoryContentsObject>(tc, address, address + requestedByteSize);
    return Ref<sym::Value>::MakeShared(
      std::move(variableContext), symbol.Name(), symbol, u32{ 0 }, std::move(memoryObject));
  }

  auto res = tc.SafeRead(address, requestedByteSize);
  if (!res.is_expected()) {
    PANIC("Expected read to succeed");
  }
  DBGLOG(dap, "[eager read]: {}:+{}", address, requestedByteSize);
  auto memory_object =
    std::make_shared<EagerMemoryContentsObject>(address, address + requestedByteSize, res.take_value());
  return Ref<Value>::MakeShared(
    std::move(variableContext), symbol.Name(), symbol, u32{}, std::move(memory_object));
}

/* static */
Ref<Value>
MemoryContentsObject::CreateSyntheticVariable(
  const VariableContext &varContext, AddrPtr address, SyntheticType type, bool lazy)
{
  const auto requestedByteSize = type.mLayoutType->SizeBytes() * type.mCount;

  auto variableContext = VariableContext::CloneFrom(Tracer::NewVariablesReference(), varContext);

  auto *supervisor = variableContext->mTask->GetSupervisor();
  const auto ref = variableContext->mId;
  if (lazy) {
    auto memoryObject =
      std::make_shared<LazyMemoryContentsObject>(*supervisor, address, address + requestedByteSize);
    return Ref<sym::Value>::MakeShared(
      std::move(variableContext), std::format("${}", ref), type, u32{}, std::move(memoryObject));
  }

  auto res = supervisor->SafeRead(address, requestedByteSize);
  DBGLOG(dap, "[eager read]: {}:+{} (success={})", address, requestedByteSize, res.is_expected());
  // A failed (or partial) read keeps whatever bytes were read. Views into missing bytes come back empty.
  auto bytes = res.is_expected() ? res.take_value() : std::move(res.take_error().mBytes);
  auto memoryObject =
    std::make_shared<EagerMemoryContentsObject>(address, address + requestedByteSize, std::move(bytes));
  return Ref<Value>::MakeShared(
    std::move(variableContext), std::format("${}", ref), type, u32{}, std::move(memoryObject));
}

/* static */
Ref<Value>
MemoryContentsObject::CreateSyntheticVariable(
  tc::SupervisorState &tc, TaskInfo *task, SymbolFile *symbolFile, AddrPtr address, SyntheticType type, bool lazy)
{
  const auto requestedByteSize = type.mLayoutType->SizeBytes() * type.mCount;

  auto variableContext = VariableContext::CreateFreestanding(task, symbolFile, Tracer::NewVariablesReference());

  const auto ref = variableContext->mId;
  if (lazy) {
    auto memoryObject = std::make_shared<LazyMemoryContentsObject>(tc, address, address + requestedByteSize);
    return Ref<sym::Value>::MakeShared(
      std::move(variableContext), std::format("${}", ref), type, u32{}, std::move(memoryObject));
  }

  auto res = tc.SafeRead(address, requestedByteSize);
  DBGLOG(dap, "[eager read]: {}:+{} (success={})", address, requestedByteSize, res.is_expected());
  auto bytes = res.is_expected() ? res.take_value() : std::move(res.take_error().mBytes);
  auto memoryObject =
    std::make_shared<EagerMemoryContentsObject>(address, address + requestedByteSize, std::move(bytes));
  return Ref<Value>::MakeShared(
    std::move(variableContext), std::format("${}", ref), type, u32{}, std::move(memoryObject));
}

SynthesizedMemoryContentsObject::SynthesizedMemoryContentsObject(
  AddrPtr start, AddrPtr end, MemoryContentBytes bytes) noexcept
    : MemoryContentsObject(start, end), mContents(std::move(bytes))
{
}

bool
SynthesizedMemoryContentsObject::Refresh(tc::SupervisorState &supervisor) noexcept
{
  return true;
}

std::span<const u8>
SynthesizedMemoryContentsObject::RawView() noexcept
{
  return mContents->Span();
}

std::span<const u8>
SynthesizedMemoryContentsObject::View(u32 offset, u32 size) noexcept
{
  return ClampedSubspan(mContents->Span(), offset, size);
}

} // namespace mdb::sym