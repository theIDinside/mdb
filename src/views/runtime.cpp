/** LICENSE TEMPLATE */
// Connects views to the rest of the debugger: reading target memory, producing DAP variables and child values.

// mdb
#include <interface/tracee_command/supervisor_state.h>
#include <symbolication/objfile.h>
#include <symbolication/type.h>
#include <symbolication/value.h>
#include <symbolication/variable_reference.h>
#include <task.h>
#include <tracer.h>
#include <utils/byte_buffer.h>
#include <utils/logger.h>
#include <views/cursor_cache.h>
#include <views/evaluator.h>
#include <views/registry.h>
#include <views/resolver.h>
#include <views/target_reader.h>
#include <views/text.h>
#include <views/view_serializer.h>

// std
#include <algorithm>
#include <cassert>
#include <cstring>
#include <format>
#include <vector>

namespace mdb::views {

// When the client asks for children without a range, this is how many we hand out.
static constexpr u64 kDefaultChildrenPage = 1000;

namespace {

tc::SupervisorState *
SupervisorOf(const sym::Value &value) noexcept
{
  const auto context = value.GetVariableContext();
  if (!context || context->mTask == nullptr) {
    return nullptr;
  }
  return context->mTask->GetSupervisor();
}

// The bytes of the object the view looks at: the value itself, or the base class sub-object a view matched.
std::span<const u8>
RootBytes(const CodeObject &view, const sym::Value &value) noexcept
{
  const auto bytes = value.MemoryView();
  return view.mRootOffset <= bytes.size() ? bytes.subspan(view.mRootOffset) : std::span<const u8>{};
}

u64
RootAddress(const CodeObject &view, const sym::Value &value) noexcept
{
  return value.Address().GetRaw() + view.mRootOffset;
}

std::string
FormatError(const CodeObject &view, const EvalError &error)
{
  return std::format(
    "<view error: {}:{}:{}: {}>", view.mSource->mPath, error.mLoc.mLine, error.mLoc.mColumn, error.mMessage);
}

void
AssertCheckpointSafe(const ProgramState &state) noexcept
{
  for (const EvalValue &value : state.mLocals) {
    assert(value.Kind() != ValueKind::Text);
  }
  for (const EvalValue &value : state.mOperands) {
    assert(value.Kind() != ValueKind::Text);
  }
}

} // namespace

std::optional<std::pmr::string>
ViewSerializer::SerializeVariable(const sym::Value &value, std::pmr::memory_resource *allocator) noexcept
{
  const sym::CanonicalType type = value.GetType();
  const CodeObject *view = mRegistry.Lookup(type.TypeEntry());

  std::string summary;
  std::optional<u64> indexedVariables;
  bool expandable = true;

  if (view == nullptr) {
    summary = std::format("{}", *type);
  } else if (view->mBindError) {
    summary = std::format("<view error: {}>", view->mBindError->ToString());
  } else {
    TargetReader reader{ SupervisorOf(value) };
    Root root{
      .mType = view->mRootType, .mAddress = RootAddress(*view, value), .mBytes = RootBytes(*view, value)
    };
    Evaluator evaluator{ *view, reader, std::span{ &root, 1 } };

    if (view->mSummaryKind == SummarySpec::Kind::Default) {
      summary = std::format("{}", *type);
    } else if (auto result = evaluator.Summary(); result) {
      summary = std::move(*result);
    } else {
      summary = FormatError(*view, result.error());
    }

    switch (view->mChildrenKind) {
    case ChildrenSpec::Kind::None:
      expandable = false;
      break;
    case ChildrenSpec::Kind::Expression:
      // If producing the children fails, the value stays expandable: expanding it shows the error.
      if (auto children = evaluator.Children(); children) {
        indexedVariables = children->mCount;
        expandable = children->mCount > 0;
      }
      break;
    case ChildrenSpec::Kind::Block:
    case ChildrenSpec::Kind::Default:
      break;
    }
  }

  std::string json;
  json.reserve(128 + summary.size());
  json.append(R"({ "name": ")");
  AppendJsonEscaped(value.mName.StringView(), json);
  json.append(R"(", "value": ")");
  AppendJsonEscaped(summary, json);
  json.append(R"(", "type": ")");
  AppendJsonEscaped(std::format("{}", *type), json);
  std::format_to(std::back_inserter(json), R"(", "variablesReference": {})", expandable ? value.ReferenceId() : 0);
  if (value.HasTargetAddress()) {
    std::format_to(std::back_inserter(json), R"(, "memoryReference": "{}")", value.Address());
  }
  if (indexedVariables && expandable) {
    std::format_to(std::back_inserter(json), R"(, "indexedVariables": {})", *indexedVariables);
  }
  json.append(" }");
  return std::pmr::string{ json, allocator };
}

std::optional<std::pmr::string>
ViewErrorSerializer::SerializeVariable(const sym::Value &value, std::pmr::memory_resource *allocator) noexcept
{
  std::string json{ R"({ "name": "[view error]", "value": ")" };
  AppendJsonEscaped(value.mName.StringView(), json);
  json.append(R"(", "variablesReference": 0 })");
  return std::pmr::string{ json, allocator };
}

static ViewErrorSerializer sErrorSerializer;

static Ref<sym::Value>
MakeErrorChild(const VariableContext &context, sym::Value &parent, std::string message) noexcept
{
  auto child = Ref<sym::Value>::MakeShared(VariableContext::CloneFrom(0, context),
    std::move(message),
    *parent.GetType().TypeEntry(),
    0U,
    parent.TakeMemoryReference(),
    &sErrorSerializer);
  return child;
}

static Ref<sym::Value>
MakeYieldedChild(const VariableContext &context,
  tc::SupervisorState &supervisor,
  const CodeObject &view,
  const YieldedElement &element,
  u64 index) noexcept
{
  sym::Type *type = view.Type(element.mType).mDwarf;
  const u64 size = type->Size();
  std::shared_ptr<sym::MemoryContentsObject> memory;
  if (element.mAddress) {
    const AddrPtr start{ *element.mAddress };
    memory = std::make_shared<sym::LazyMemoryContentsObject>(supervisor, start, offset(start, size));
  } else {
    auto buffer = ByteBuffer::Create(size);
    u64 bits = element.mValue.Bits();
    if (view.Type(element.mType).mKind == TypeKind::Float) {
      if (size == sizeof(float)) {
        const auto value = static_cast<float>(element.mValue.AsFloat());
        u32 floatBits = 0;
        std::memcpy(&floatBits, &value, sizeof(float));
        bits = floatBits;
      } else {
        const double value = element.mValue.AsFloat();
        std::memcpy(&bits, &value, sizeof(double));
      }
    }
    std::vector<u8> bytes(static_cast<size_t>(size), 0);
    for (size_t i = 0; i < std::min<u64>(size, sizeof(bits)); ++i) {
      bytes[i] = static_cast<u8>(bits >> (i * 8));
    }
    (void)buffer->Write(bytes);
    memory = std::make_shared<sym::SynthesizedMemoryContentsObject>(
      AddrPtr{ nullptr }, AddrPtr{ size }, std::move(buffer));
  }
  const bool isPrimitive = sym::CanonicalType::From(*type)->IsPrimitive();
  auto childContext =
    isPrimitive ? VariableContext::CloneFrom(0, context) : Tracer::CloneFromVariableContext(context);
  std::string name = element.mKey.value_or(std::format("[{}]", index));
  auto child = Ref<sym::Value>::MakeShared(childContext, std::move(name), *type, 0U, std::move(memory));
  ObjectFile::SetSerializerFor(*child);
  if (!isPrimitive && childContext->mId > 0) {
    context.mTask->CacheValueObject(childContext->mId, child);
  }
  return child;
}

static bool
RunBlock(GlobalCursorCache::Cursor &cursor,
  tc::SupervisorState *supervisor,
  Evaluator &evaluator,
  const CodeObject &view,
  const VariableContext &context,
  sym::Value &value,
  sym::ValueRange range,
  std::vector<Ref<sym::Value>> &result) noexcept
{
  const u64 start = range.start.value_or(0);
  const u64 count = range.count.value_or(kDefaultChildrenPage);
  if (cursor.mCheckpoints.empty()) {
    auto thisPointer = evaluator.ThisArgument();
    if (!thisPointer) {
      result.emplace_back(MakeErrorChild(context, value, FormatError(view, thisPointer.error())));
      return true;
    }
    ProgramState initialState = evaluator.Start(view.mChildrenFunction, std::span{ &*thisPointer, 1 });
    AssertCheckpointSafe(initialState);
    cursor.mCheckpoints.push_back(
      GlobalCursorCache::Checkpoint{ .mYieldIndex = 0, .mState = std::move(initialState) });
  }

  const GlobalCursorCache::Checkpoint *checkpoint = nullptr;
  for (const auto &candidate : cursor.mCheckpoints) {
    if (candidate.mYieldIndex <= start &&
        (checkpoint == nullptr || candidate.mYieldIndex > checkpoint->mYieldIndex)) {
      checkpoint = &candidate;
    }
  }
  if (checkpoint == nullptr) {
    checkpoint = &cursor.mCheckpoints.front();
  }
  ProgramState state = checkpoint->mState;
  u64 index = checkpoint->mYieldIndex;
  u64 budgetLeft = 1'000'000;
  std::vector<std::pair<u64, YieldedElement>> collected;
  std::optional<Ref<sym::Value>> errorChild;

  while (collected.size() < count && !state.mDone) {
    const u64 maxYields = 100 - (index % 100);
    const auto onYield = [&](const YieldedElement &element) {
      if (index >= start && collected.size() < count) {
        collected.emplace_back(index, element);
      }
      ++index;
    };
    auto run = evaluator.Run(state, RunLimits{ .mMaxYields = maxYields, .mBudget = budgetLeft }, onYield);
    if (!run) {
      errorChild = MakeErrorChild(context, value, FormatError(view, run.error()));
      break;
    }
    budgetLeft -= run->mSteps;
    if (run->mStop == RunStop::Budget) {
      errorChild = MakeErrorChild(context, value, std::format("stopped after {} steps", 1'000'000 - budgetLeft));
      break;
    }
    if (run->mStop == RunStop::Paused && index % 100 == 0 && index > cursor.mCheckpoints.back().mYieldIndex) {
      AssertCheckpointSafe(state);
      cursor.mCheckpoints.push_back(GlobalCursorCache::Checkpoint{ .mYieldIndex = index, .mState = state });
    }
  }

  result.reserve(collected.size() + (errorChild ? 1 : 0));
  for (const auto &[yieldIndex, element] : collected) {
    result.push_back(MakeYieldedChild(context, *supervisor, view, element, yieldIndex));
  }
  if (errorChild) {
    result.push_back(std::move(*errorChild));
  }
  return true;
}

bool
ResolveUsingView(const VariableContext &context,
  sym::Value &value,
  sym::ValueRange range,
  std::vector<Ref<sym::Value>> &result) noexcept
{

  auto &registry = views::ViewRegistry::Get();
  if (registry.IsEmpty()) {
    return false;
  }

  const CodeObject *view = registry.Lookup(value.GetType().TypeEntry());

  if (!view) {
    return false;
  }

  if (view->mBindError || view->mChildrenKind == ChildrenSpec::Kind::Default) {
    return false;
  }

  tc::SupervisorState *supervisor = SupervisorOf(value);
  if (supervisor == nullptr) {
    return false;
  }

  // Success, but result == empty
  if (view->mChildrenKind == ChildrenSpec::Kind::None) {
    return true;
  }

  TargetReader reader{ supervisor };
  Root root{ .mType = view->mRootType, .mAddress = RootAddress(*view, value), .mBytes = RootBytes(*view, value) };
  Evaluator evaluator{ *view, reader, std::span{ &root, 1 } };

  if (view->mChildrenKind == ChildrenSpec::Kind::Block) {
    return RunBlock(views::GlobalCursorCache::Get().For(context.mId, registry.Generation()),
      supervisor,
      evaluator,
      *view,
      context,
      value,
      range,
      result);
  }

  auto children = evaluator.Children();
  if (!children) {
    result.push_back(MakeErrorChild(context, value, FormatError(*view, children.error())));
    return true;
  }

  const BoundType &elementType = view->Type(children->mElement);
  sym::Type *element = elementType.mDwarf;
  const u64 elementSize = element->Size();
  const u64 start = range.start.value_or(0);
  if (start >= children->mCount || elementSize == 0) {
    return false;
  }
  const u64 count = std::min<u64>(range.count.value_or(kDefaultChildrenPage), children->mCount - start);

  const AddrPtr first{ children->mBase + (start * elementSize) };
  // One read of the target for the whole page, shared by all elements.
  auto memory =
    std::make_shared<sym::LazyMemoryContentsObject>(*supervisor, first, offset(first, count * elementSize));

  const bool isPrimitive = sym::CanonicalType::From(*element)->IsPrimitive();
  // Primitive elements aren't expandable, so they can all share one context.
  const auto primitiveContext = isPrimitive ? VariableContext::CloneFrom(0, context) : nullptr;

  result.reserve(count);
  for (u64 i = 0; i < count; ++i) {
    auto elementContext = isPrimitive ? primitiveContext : Tracer::CloneFromVariableContext(context);
    auto child = Ref<sym::Value>::MakeShared(
      elementContext, std::format("[{}]", start + i), *element, static_cast<u32>(i * elementSize), memory);
    ObjectFile::SetSerializerFor(*child);
    if (elementContext->mId > 0) {
      context.mTask->CacheValueObject(elementContext->mId, child);
    }
    result.push_back(std::move(child));
  }

  return true;
}

} // namespace mdb::views
