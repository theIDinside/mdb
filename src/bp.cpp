/** LICENSE TEMPLATE */
#include "bp.h"

// mdb
#include <common/panic.h>
#include <events/stop_event.h>
#include <interface/dap/events.h>
#include <interface/dap/interface.h>
#include <interface/tracee_command/supervisor_state.h>
#include <mdbjs/bpjs.h>
#include <mdbjs/mdbjs.h>
#include <mdbsys/ptrace.h>
#include <symbolication/callstack.h>
#include <symbolication/dwarf/typeread.h>
#include <symbolication/fnsymbol.h>
#include <symbolication/objfile.h>
#include <symbolication/value.h>
#include <task.h>
#include <tracer.h>
#include <utils/logger.h>
#include <utils/todo.h>
#include <views/evaluator.h>
#include <views/parser.h>
#include <views/registry.h>
#include <views/target_reader.h>

// std
#include <algorithm>
#include <format>
#include <type_traits>

#define BP_KEEP(STOP)                                                                                             \
  BreakpointHitEventResult { this, STOP }
#define BP_RETIRE(STOP)                                                                                           \
  BreakpointHitEventResult { this, STOP, BreakpointOp::Retire }
namespace mdb {

BreakpointLocation::BreakpointLocation(AddrPtr address, u8 original) noexcept
    : mAddress(address), mOriginalByte(original)
{
  DBGBUFLOG(control, "[breakpoint loc]: Constructed breakpoint location at {}", address);
}

BreakpointLocation::BreakpointLocation(
  AddrPtr address, u8 original, std::unique_ptr<LocationSourceInfo> sourceLocationInfo) noexcept
    : mAddress(address), mSourceLocation(std::move(sourceLocationInfo)), mOriginalByte(original)
{
  DBGLOG(core, "[breakpoint loc]: Constructed breakpoint location at {}", address);
}

BreakpointLocation::~BreakpointLocation() noexcept
{
  MDB_ASSERT(!mInstalled, "Breakpoint location was enabled while being destroyed - this is a hard error");
  MDB_ASSERT(mUserMapping.empty(),
    "This breakpoint location was destroyed when having active (but destroyed) users registered to it");
  DBGLOG(core, "[breakpoint loc]: Destroying breakpoint location at {}", mAddress);
}

/*static*/ BreakpointLocation::Ref
BreakpointLocation::CreateLocation(AddrPtr address, u8 original) noexcept
{
  return RefPtr<BreakpointLocation>::MakeShared(address, original);
}
/*static*/ BreakpointLocation::Ref
BreakpointLocation::CreateLocationWithSource(
  AddrPtr address, u8 original, std::unique_ptr<LocationSourceInfo> sourceLocationInfo) noexcept
{
  return RefPtr<BreakpointLocation>::MakeShared(address, original, std::move(sourceLocationInfo));
}

bool
BreakpointLocation::AnyUsersActive() const noexcept
{
  return std::any_of(
    mUserMapping.begin(), mUserMapping.end(), [](const auto user) { return user->IsEnabledAndInstalled(); });
}

std::vector<u32>
BreakpointLocation::GetUserIds() const noexcept
{
  std::vector<u32> usr{};
  usr.reserve(mUserMapping.size());
  for (auto *u : mUserMapping) {
    usr.push_back(u->mId);
  }
  return usr;
}

const LocationSourceInfo *
BreakpointLocation::GetSourceLocationInfo() const noexcept
{
  if (mSourceLocation) {
    return mSourceLocation.get();
  }
  return nullptr;
}

bool
BreakpointLocation::RemoveUserOfThis(tc::SupervisorState &controller, UserBreakpoint &breakpoint) noexcept
{
  auto it = std::ranges::find(mUserMapping, &breakpoint);
  if (it == std::end(mUserMapping)) {
    return false;
  }

  mUserMapping.erase(it);
  if (!AnyUsersActive()) {
    Disable(controller.TaskLeaderTid(), controller);
  }
  return true;
}

void
BreakpointLocation::Enable(Tid taskId, tc::SupervisorState &controller) noexcept
{
  if (mUserMapping.empty()) {
    MDB_ASSERT(!mInstalled, "A breakpoint location, with no users, but is installed?");
    DBGLOG(core, "BreakpointLocation has no users.");
    return;
  }

  if (!mInstalled) {
    const auto res = controller.EnableBreakpoint(taskId, *this);
    switch (res.kind) {
    case tc::TaskExecuteResult::Ok:
      mInstalled = true;
      break;
    case tc::TaskExecuteResult::Error:
    case tc::TaskExecuteResult::None:
      break;
    }
  }
}

void
BreakpointLocation::Disable(Tid taskId, tc::SupervisorState &controller) noexcept
{
  if (mInstalled) {
    const auto result = controller.DisableBreakpoint(taskId, *this);
    VERIFY(result.kind == tc::TaskExecuteResult::Ok,
      "[tracer:{}.{}] Failed to disable breakpoint",
      controller.TaskLeaderTid(),
      taskId);
    mInstalled = false;
  }
}

bool
BreakpointLocation::IsInstalled() const noexcept
{
  return mInstalled;
}

void
BreakpointLocation::AddUser(tc::SupervisorState &controller, UserBreakpoint &breakpoint) noexcept
{
  MDB_ASSERT(
    std::none_of(mUserMapping.begin(), mUserMapping.begin(), [&breakpoint](auto v) { return v != &breakpoint; }),
    "Expected user breakpoint to not be registered with bploc");
  mUserMapping.push_back(&breakpoint);

  if (!mInstalled) {
    breakpoint.Enable(controller);
  }
}

UserBreakpoint::UserBreakpoint(RequiredUserParameters param, LocationUserKind locationUserKind) noexcept
    : mId(param.mBreakpointId), mTid(param.mTaskId), mKind(locationUserKind),
      mHitCondition(param.mTimesToHit.value_or(0))
{

  if (param.mBreakpointLocationResult.is_expected()) {
    mBreakpointLocation = std::move(param.mBreakpointLocationResult.take_value());
    mInstallError = nullptr;
  } else {
    mBreakpointLocation = nullptr;
    mInstallError = std::make_unique<BreakpointError>(param.mBreakpointLocationResult.take_error());
  }

  if (mBreakpointLocation != nullptr) {
    mBreakpointLocation->AddUser(param.mControl, *this);
  }
}

UserBreakpoint::~UserBreakpoint() noexcept = default;

BreakpointLocation::Ref
UserBreakpoint::GetLocation() noexcept
{
  return mBreakpointLocation;
}

bool
UserBreakpoint::IsEnabledAndInstalled() noexcept
{
  return mEnabledByUser && mBreakpointLocation != nullptr && mBreakpointLocation->IsInstalled();
}

void
UserBreakpoint::Enable(tc::SupervisorState &controller) noexcept
{
  if (mEnabledByUser && mBreakpointLocation != nullptr) {
    mEnabledByUser = true;
    mBreakpointLocation->Enable(controller.TaskLeaderTid(), controller);
  }
}

void
UserBreakpoint::Disable(tc::SupervisorState &controller) noexcept
{
  if (mEnabledByUser && mBreakpointLocation != nullptr) {
    mEnabledByUser = false;
    if (mBreakpointLocation->AnyUsersActive()) {
      mBreakpointLocation->Disable(controller.TaskLeaderTid(), controller);
    }
  }
}

std::optional<BreakpointHitEventResult>
UserBreakpoint::EvaluateStopCondition(TaskInfo &t) noexcept
{
  if (!mExpression) {
    return {};
  }

  BreakpointHitEventResult result{ this };
  result.mResult = EventResult::Resume;
  if (!mExpression->Run(&result, t)) {
    return result;
  }
  return result;
}

std::optional<AddrPtr>
UserBreakpoint::Address() const noexcept
{
  if (mBreakpointLocation == nullptr) {
    return {};
  }
  return mBreakpointLocation->Address();
}

bool
UserBreakpoint::IsVerified() const noexcept
{
  return mBreakpointLocation != nullptr;
}

std::optional<u32>
UserBreakpoint::Line() const noexcept
{
  if (!mBreakpointLocation) {
    return {};
  }

  if (const auto *src = mBreakpointLocation->GetSourceLocationInfo(); src) {
    return src->mLineNumber;
  }
  return {};
}

std::optional<u32>
UserBreakpoint::Column() const noexcept
{
  if (!mBreakpointLocation) {
    return {};
  }

  if (const auto *src = mBreakpointLocation->GetSourceLocationInfo(); src) {
    return src->mColumnNumber;
  }
  return {};
}

std::optional<std::string_view>
UserBreakpoint::GetSourceFile() const noexcept
{
  if (!mBreakpointLocation) {
    return {};
  }

  if (const auto *src = mBreakpointLocation->GetSourceLocationInfo(); src) {
    return std::optional{ std::string_view{ src->mSourceFile } };
  }
  return {};
}

std::optional<std::pmr::string>
UserBreakpoint::GetErrorMessage(std::pmr::memory_resource *rsrc) const noexcept
{
  return mdb::transform(mInstallError, [t = this, rsrc](const BreakpointError &err) {
    std::pmr::string message{ rsrc };
    auto it = std::back_inserter(message);
    std::visit(
      [t, &it](const auto &e) {
        using T = std::remove_cvref_t<decltype(e)>;
        if constexpr (std::is_same_v<T, MemoryError>) {
          std::format_to(it, "Could not write to {} ({})", e.mRequestedAddress, strerror(e.mErrorNumber));
        } else if constexpr (std::is_same_v<T, ResolveError>) {
          const auto *spec = t->UserProvidedSpec();
          if (spec) {
            std::format_to(it, "Could not resolve using spec: {}", *spec);
          } else {
            std::format_to(it, "Could not resolve breakpoint using spec");
          }
        } else {
          static_assert(always_false<T>, "Should be unreachable");
        }
      },
      err);
    return message;
  });
}

void
UserBreakpoint::UpdateLocation(Ref<BreakpointLocation> breakpointLocation) noexcept
{
  mBreakpointLocation = std::move(breakpointLocation);
}

void
UserBreakpoint::SetExpression(std::unique_ptr<js::JsBreakpointFunction> expression) noexcept
{
  mExpression = std::move(expression);
}

/* virtual */
BreakpointSpecification *
UserBreakpoint::UserProvidedSpec() const noexcept
{
  return nullptr;
}

/* virtual */
Ref<UserBreakpoint>
UserBreakpoint::CloneBreakpoint(
  ProcessBreakpointsManager &breakpointStorage, tc::SupervisorState &tc, Ref<BreakpointLocation> bp) noexcept
{
  IGNORE_ARGS(breakpointStorage, tc, bp);
  PANIC("Generic user breakpoint should not be cloned. This is icky that I've done it like this.");
  return nullptr;
}

Breakpoint::Breakpoint(
  RequiredUserParameters param, LocationUserKind kind, std::unique_ptr<BreakpointSpecification> spec) noexcept
    : UserBreakpoint(std::move(param), kind), mBreakpointSpec(std::move(spec))
{
  if (mBreakpointSpec->mCondition) {
    auto res = js::JsBreakpointFunction::CreateJsBreakpointFunction(
      js::Scripting::Get().GetContext(), *mBreakpointSpec->mCondition);
    if (res.has_value()) {
      SetExpression(std::move(*res));
    }
  }
}

static bool
HookRequestedResume(EventResult result) noexcept
{
  return result == EventResult::Resume;
}

constexpr static BreakpointBehavior
DetermineBehavior(EventResult evaluationResult, tc::SupervisorState &tc) noexcept
{
  MDB_ASSERT(evaluationResult != EventResult::Resume, "Requires a stop behavior (or default)");
  if (evaluationResult == EventResult::Stop) {
    return BreakpointBehavior::StopOnlyThreadThatHit;
  }
  if (evaluationResult == EventResult::StopAll) {
    return BreakpointBehavior::StopAllThreadsWhenHit;
  }
  // Get session-wide configuration of breakpoint behavior.
  return tc.GetBreakpointBehavior();
}

BreakpointHitEventResult
Breakpoint::OnHit(tc::SupervisorState &controller, TaskInfo &task) noexcept
{
  DBGBUFLOG(control, "[{}:bkpt]: bp {} hit", task.mTid, mId);
  IncrementHitCount();

  const auto evaluatedResult = EvaluateStopCondition(task).value_or(BreakpointHitEventResult{ this });

  if (HookRequestedResume(evaluatedResult.mResult)) {
    return evaluatedResult;
  };

  if (DetermineBehavior(evaluatedResult.mResult, controller) == BreakpointBehavior::StopAllThreadsWhenHit) {
    controller.StopAllTasks(
      [&]() { controller.EmitStoppedAtBreakpoints({ .pid = 0, .tid = task.mTid }, mId, true); });
  } else {
    controller.EmitStoppedAtBreakpoints({ .pid = 0, .tid = task.mTid }, mId, false);
  }
  return BP_KEEP(EventResult::Stop);
}

BreakpointSpecification *
Breakpoint::UserProvidedSpec() const noexcept
{
  return mBreakpointSpec.get();
}

Ref<UserBreakpoint>
Breakpoint::CloneBreakpoint(ProcessBreakpointsManager &breakpointStorage,
  tc::SupervisorState &controller,
  Ref<BreakpointLocation> breakpointLocation) noexcept
{

  auto breakpoint = Ref<Breakpoint>::MakeShared(RequiredUserParameters{ .mTaskId = mTid,
                                                  .mBreakpointId = breakpointStorage.NewBreakpointId(),
                                                  .mBreakpointLocationResult = std::move(breakpointLocation),
                                                  .mTimesToHit = {},
                                                  .mControl = controller },
    mKind,
    mBreakpointSpec->Clone());

  breakpointStorage.AddUser(breakpoint);
  MDB_ASSERT(!breakpoint->GetLocation()->GetUserIds().empty(), "Breakpoint location should have user now!");
  return breakpoint;
}

FinishBreakpoint::FinishBreakpoint(RequiredUserParameters param, Tid stopOnlyTaskTid) noexcept
    : UserBreakpoint(std::move(param), LocationUserKind::FinishFunction), mStopOnlyTid(stopOnlyTaskTid)
{
}

BreakpointHitEventResult
FinishBreakpoint::OnHit(tc::SupervisorState &controller, TaskInfo &task) noexcept
{
  if (task.mTid != mStopOnlyTid) {
    return BP_KEEP(Resume);
  }
  DBGBUFLOG(control, "Hit finish_bp_t {}", mId);
  const auto all_stopped = controller.IsAllStopped();

  // TODO(simon): This is the point where we should read the value produced by the function we returned from.
  if (all_stopped) {
    controller.EmitSteppedStop({ controller.TaskLeaderTid(), mTid }, "Finished function", true);
  } else {
    controller.StopAllTasks([&controller, tid = mTid]() {
      controller.EmitSteppedStop({ controller.TaskLeaderTid(), tid }, "Finished function", true);
    });
  }
  return BP_RETIRE(Stop);
}

CodeInjectionBoundaryBreakpoint::CodeInjectionBoundaryBreakpoint(RequiredUserParameters param) noexcept
    : UserBreakpoint(std::move(param), LocationUserKind::Address)
{
}

BreakpointHitEventResult
CodeInjectionBoundaryBreakpoint::OnHit(tc::SupervisorState &controller, TaskInfo &task) noexcept
{
  DBGBUFLOG(control, "Task {} hit code injection boundary breakpoint at {}", task.mTid, Address().value());
  (void)controller;
  (void)task;
  return BP_KEEP(None);
}

ResumeToBreakpoint::ResumeToBreakpoint(RequiredUserParameters param, Tid tid) noexcept
    : UserBreakpoint(std::move(param), LocationUserKind::ResumeTo), mStopOnlyTid(tid)
{
}

BreakpointHitEventResult
ResumeToBreakpoint::OnHit([[maybe_unused]] tc::SupervisorState &controller, TaskInfo &task) noexcept
{
  if (task.mTid == mStopOnlyTid) {
    DBGBUFLOG(control, "Hit resume_bp_t {}", mId);
    return BP_RETIRE(Resume);
  }
  return BP_KEEP(None);
}

Logpoint::Logpoint(RequiredUserParameters param,
  std::string_view logExpression,
  std::unique_ptr<BreakpointSpecification> specification) noexcept
    : Breakpoint(std::move(param), LocationUserKind::LogPoint, std::move(specification)), mMessage(logExpression)
{
  if (mBreakpointSpec->mCondition) {
    DBGLOG(warning, "condition on a logpoint is ignored");
  }
}

bool
Logpoint::EnsureBound(tc::SupervisorState &controller, sym::Frame &frame) noexcept
{
  auto *obj = frame.GetSymbolFile()->GetObjectFile();
  auto &registry = views::ViewRegistry::Get();
  if (mBound && mBoundPc == frame.FramePc() && mBoundObjectFile == obj &&
      mBoundGeneration == registry.Generation()) {
    return mBound->mCodeObject && !mBound->mCodeObject->mBindError;
  }
  if (mBound && mBoundGeneration != registry.Generation()) {
    // Reloaded views can fix, or cause, a bind error
    mReportedBindError = false;
  }
  mBoundPc = frame.FramePc();
  mBoundObjectFile = obj;
  mBoundGeneration = registry.Generation();
  mRootSymbols.clear();
  mBindFailed = false;
  auto *fn = frame.MaybeGetFullSymbolInfo();
  if (!fn) {
    return false;
  }
  if (!fn->IsResolved()) {
    sym::dw::FunctionSymbolicationContext symbolicationContext{ *obj, frame };
    symbolicationContext.ProcessSymbolInformation();
  }
  const AddrPtr pc = frame.GetSymbolFile()->UnrelocateAddress(frame.FramePc());
  std::vector<const sym::SymbolBlock *> blocks;
  for (const auto &block : fn->GetFrameLocalVariableBlocks()) {
    if (block.ContainsPc(pc)) {
      blocks.push_back(&block);
    }
  }
  std::ranges::sort(blocks, [](auto *a, auto *b) {
    return (static_cast<u64>(a->EndPc()) - static_cast<u64>(a->StartPc())) <
           (static_cast<u64>(b->EndPc()) - static_cast<u64>(b->StartPc()));
  });
  std::vector<sym::Symbol *> symbols;
  std::vector<views::FrameVariable> variables;
  for (const sym::SymbolBlock *block : blocks) {
    for (auto &symbol : const_cast<sym::SymbolBlock *>(block)->mSymbols) {
      symbols.push_back(&symbol);
      variables.push_back({ symbol.Name(), symbol.Type().TypeEntry() });
    }
  }
  for (auto &symbol : const_cast<sym::SymbolBlock &>(fn->GetFunctionArguments()).mSymbols) {
    symbols.push_back(&symbol);
    variables.push_back({ symbol.Name(), symbol.Type().TypeEntry() });
  }
  const auto *loc = GetLocation()->GetSourceLocationInfo();
  const auto path = loc ? std::string_view{ loc->mSourceFile } : std::string_view{ "unknown" };
  const auto line = loc ? *loc->mLineNumber : 0;
  auto tmpl = views::ParseFrameTemplate(std::format("<logpoint {}:{}>", path, line), mMessage);
  const views::ViewLookup lookup = [&registry](sym::Type *type) -> views::ViewLookupResult {
    type->ResolveType();
    return views::ViewLookupResult{ .mView = registry.Lookup(type) };
  };
  mBound = views::BindFrameTemplate(std::move(tmpl), variables, obj, lookup);
  for (u32 i : mBound->mRootVariables) {
    mRootSymbols.push_back(symbols[i]);
  }
  if (mBound->mCodeObject && mBound->mCodeObject->mBindError) {
    mBindFailed = true;
  }
  if (mBound->mCodeObject && mBound->mCodeObject->mBindError && !mReportedBindError) {
    mReportedBindError = true;
    auto diagnostic = mBound->mCodeObject->mBindError->mMessage;
    auto *event = new ui::dap::OutputEvent{
      controller.TaskLeaderTid(), "console", std::format("logpoint {}:{}: {}\n", path, line, diagnostic)
    };
    if (loc) {
      event->mSourcePath = std::string{ loc->mSourceFile };
      event->mLine = *loc->mLineNumber;
    }
    controller.GetDebugAdapterProtocolClient()->PostDapEvent(event);
    controller.EmitBreakpointEvent("changed", *this, diagnostic);
  }
  return mBound->mCodeObject && !mBound->mCodeObject->mBindError;
}

std::string
Logpoint::Evaluate(tc::SupervisorState &controller, sym::Frame &frame) noexcept
{
  std::vector<sym::VariableStorage> storage;
  std::vector<views::Root> roots;
  storage.reserve(mRootSymbols.size());
  roots.reserve(mRootSymbols.size());
  for (u32 i = 0; i < mRootSymbols.size(); ++i) {
    auto value = sym::ResolveVariableStorage(controller, frame, *mRootSymbols[i]);
    if (value) {
      storage.push_back(std::move(*value));
    } else {
      storage.push_back({ .mUnavailable = value.error() });
    }
    auto &s = storage.back();
    roots.push_back({ .mType = mBound->mCodeObject->mRootTypes[i],
      .mAddress = s.mAddress ? std::optional<u64>{ static_cast<u64>(*s.mAddress) } : std::nullopt,
      .mBytes = s.mBytes,
      .mUnavailable = s.mUnavailable });
  }
  views::TargetReader reader{ &controller };
  views::TargetServices services{ &controller };
  views::Evaluator evaluator{ *mBound->mCodeObject, reader, roots };
  evaluator.SetServices(&services);
  return evaluator.FormatTemplate();
}

BreakpointHitEventResult
Logpoint::OnHit(tc::SupervisorState &controller, TaskInfo &task) noexcept
{
  IncrementHitCount();
  if (controller.IsReversing()) {
    return BP_KEEP(Resume);
  }
  auto frame = controller.GetCurrentFrame(task);
  const LocationSourceInfo *loc = mBreakpointLocation->GetSourceLocationInfo();
  if (!frame.MaybeGetFullSymbolInfo()) {
    if (!mReportedBindError) {
      mReportedBindError = true;
      const auto path = loc ? std::string_view{ loc->mSourceFile } : std::string_view{ "unknown" };
      const auto line = loc ? *loc->mLineNumber : 0;
      auto *event = new ui::dap::OutputEvent{ controller.TaskLeaderTid(),
        "console",
        std::format("logpoint {}:{}: no debug info for this location\n", path, line) };
      if (loc) {
        event->mSourcePath = std::string{ loc->mSourceFile };
        event->mLine = *loc->mLineNumber;
      }
      controller.GetDebugAdapterProtocolClient()->PostDapEvent(event);
    }
    return BP_KEEP(Resume);
  }
  if (!EnsureBound(controller, frame)) {
    return BP_KEEP(Resume);
  }
  controller.BuildCallFrameStack(task, CallStackRequest::partial(1));
  auto text = Evaluate(controller, frame);

  auto *event = new ui::dap::OutputEvent{ controller.TaskLeaderTid(), "console", std::move(text) + "\n" };
  if (loc) {
    event->mSourcePath = std::string{ loc->mSourceFile };
    event->mLine = *loc->mLineNumber;
  }
  controller.GetDebugAdapterProtocolClient()->PostDapEvent(event);
  return BP_KEEP(Resume);
}

Ref<UserBreakpoint>
Logpoint::CloneBreakpoint(
  ProcessBreakpointsManager &storage, tc::SupervisorState &controller, Ref<BreakpointLocation> location) noexcept
{
  auto clone = Ref<Logpoint>::MakeShared(RequiredUserParameters{ .mTaskId = mTid,
                                           .mBreakpointId = storage.NewBreakpointId(),
                                           .mBreakpointLocationResult = std::move(location),
                                           .mTimesToHit = {},
                                           .mControl = controller },
    mMessage,
    mBreakpointSpec->Clone());
  storage.AddUser(clone);
  return clone;
}

BreakpointHitEventResult
InternalBreakpoint::OnHit(
  [[maybe_unused]] tc::SupervisorState &controller, [[maybe_unused]] TaskInfo &task) noexcept
{
  return mMaintenanceFn();
}

InternalBreakpoint::InternalBreakpoint(
  RequiredUserParameters param, std::function<BreakpointHitEventResult()> maintenanceFunc) noexcept
    : UserBreakpoint(std::move(param), LocationUserKind::Maintenance), mMaintenanceFn(std::move(maintenanceFunc))
{
}

SOLoadingBreakpoint::SOLoadingBreakpoint(RequiredUserParameters param) noexcept
    : UserBreakpoint(std::move(param), LocationUserKind::SharedObjectLoaded)
{
}

BreakpointHitEventResult
SOLoadingBreakpoint::OnHit(tc::SupervisorState &controller, [[maybe_unused]] TaskInfo &task) noexcept
{
  controller.OnSharedObjectEvent();
  // we don't stop on shared object loading breakpoints
  return BreakpointHitEventResult{
    .mUserBreakpoint = this, .mResult = EventResult::Resume, .mRetireBreakpoint = BreakpointOp::Keep
  };
}

/* static */
SharedPtr<SessionBreakpoints>
SessionBreakpoints::Create() noexcept
{
  return std::make_shared<SessionBreakpoints>();
}

void
SessionBreakpoints::Clear() noexcept
{
  mUserBreakpoints.clear();
  mUserBreakpointsAtAddress.clear();
  mSourceCodeBreakpoints.clear();
  mFunctionBreakpoints.clear();
  mInstructionBreakpoints.clear();
}

ProcessBreakpointsManager::ProcessBreakpointsManager(tc::SupervisorState &tc) noexcept : mControl(tc) {}

u16
ProcessBreakpointsManager::NewBreakpointId() noexcept
{
  return Tracer::GenerateNewBreakpointId();
}

void
ProcessBreakpointsManager::OnProcessExit() noexcept
{
  for (auto &user : AllUserBreakpoints()) {
    if (user->mBreakpointLocation) {
      // to prevent assertion. UserBreakpoints is the only type allowed to touch ->installed (via
      // friend-mechanism).
      user->mBreakpointLocation->mInstalled = false;
      user->mBreakpointLocation->mUserMapping.clear();
      user->mBreakpointLocation = nullptr;
    }
  }
  mUserBreakpoints.clear();
  mUserBreakpointsAtAddress.clear();
  mSourceCodeBreakpoints.clear();
  mFunctionBreakpoints.clear();
  mInstructionBreakpoints.clear();
}

void
ProcessBreakpointsManager::OnExec() noexcept
{
  for (auto &user : AllUserBreakpoints()) {
    if (user->mBreakpointLocation) {
      // to prevent assertion. UserBreakpoints is the only type allowed to touch ->installed (via
      // friend-mechanism).
      user->mBreakpointLocation->mInstalled = false;
      user->mBreakpointLocation->mUserMapping.clear();
      user->mBreakpointLocation.Reset();
    }
  }
  mUserBreakpointsAtAddress.clear();
}

void
ProcessBreakpointsManager::AddBreakpointLocation(const UserBreakpoint &updatedBreakpoint) noexcept
{
  mUserBreakpointsAtAddress[updatedBreakpoint.Address().value()].push_back(updatedBreakpoint.mId);
}

void
ProcessBreakpointsManager::AddUser(Ref<UserBreakpoint> breakpoint) noexcept
{
  if (breakpoint->IsVerified()) {
    AddBreakpointLocation(*breakpoint);
  }
  mUserBreakpoints[breakpoint->mId] = std::move(breakpoint);
}

void
ProcessBreakpointsManager::RemoveUserBreakpoint(u32 breakpointId) noexcept
{
  auto bp = mUserBreakpoints.find(breakpointId);
  if (bp == std::end(mUserBreakpoints)) {
    return;
  }

  if (bp->second->mBreakpointLocation) {
    bp->second->mBreakpointLocation->RemoveUserOfThis(mControl, *bp->second);
  }

  mUserBreakpoints.erase(bp);
}

Ref<BreakpointLocation>
ProcessBreakpointsManager::GetLocationAt(AddrPtr address) noexcept
{
  auto firstUserBreakpointAt = mUserBreakpointsAtAddress.find(address);
  if (firstUserBreakpointAt == std::end(mUserBreakpointsAtAddress)) {
    return nullptr;
  }
  for (const auto it : firstUserBreakpointAt->second) {
    if (const auto ubp = mUserBreakpoints.find(it); ubp != std::end(mUserBreakpoints)) {
      if (const auto loc = ubp->second->GetLocation(); loc != nullptr) {
        return loc;
      }
    }
  }
  return nullptr;
}

Ref<UserBreakpoint>
ProcessBreakpointsManager::GetUserBreakpoint(u32 id) const noexcept
{
  auto it = mUserBreakpoints.find(id);
  if (it == std::end(mUserBreakpoints)) {
    return nullptr;
  }

  return it->second;
}

std::vector<Ref<UserBreakpoint>>
ProcessBreakpointsManager::AllUserBreakpoints() const noexcept
{
  std::vector<Ref<UserBreakpoint>> result{};
  result.reserve(mUserBreakpoints.size());

  for (auto [id, user] : mUserBreakpoints) {
    result.emplace_back(std::move(user));
  }
  return result;
}

std::vector<Ref<UserBreakpoint>>
ProcessBreakpointsManager::GetNonVerified() const noexcept
{
  std::vector<Ref<UserBreakpoint>> result;
  for (const auto &[id, bp] : mUserBreakpoints) {
    if (!bp->IsVerified()) {
      result.push_back(bp);
    }
  }

  return result;
}

SourceFileBreakpointMap &
ProcessBreakpointsManager::GetBreakpointsFromSourceFile(const SourceCodeFileName &sourceCodeFileName) noexcept
{
  return mSourceCodeBreakpoints[sourceCodeFileName];
}

SourceFileBreakpointMap &
ProcessBreakpointsManager::GetBreakpointsFromSourceFile(std::string_view sourceCodeFileName) noexcept
{
  return mSourceCodeBreakpoints[std::string{ sourceCodeFileName }];
}

std::vector<std::string_view>
ProcessBreakpointsManager::GetSourceFilesWithBreakpointSpecs() const noexcept
{
  std::vector<std::string_view> result{ 15 };
  result.reserve(mSourceCodeBreakpoints.size());
  for (const auto &[source, _] : mSourceCodeBreakpoints) {
    result.emplace_back(std::string_view{ source });
  }
  return result;
}

/* static */
Expected<Ref<BreakpointLocation>, BreakpointError>
ProcessBreakpointsManager::CreateBreakpointLocation(tc::SupervisorState &supervisor, AddrPtr address) noexcept
{
  return supervisor.GetOrCreateBreakpointLocation(address);
}
} // namespace mdb
