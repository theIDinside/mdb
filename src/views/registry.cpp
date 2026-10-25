/** LICENSE TEMPLATE */
#include "registry.h"

// mdb
#include <symbolication/type.h>
#include <symbolication/value.h>
#include <utils/logger.h>
#include <views/binder.h>
#include <views/parser.h>
#include <views/view_serializer.h>

// std
#include <algorithm>
#include <format>
#include <fstream>
#include <ranges>
#include <sstream>

namespace mdb::views {

// Base class chains deeper than this are not searched for views.
static constexpr u32 kMaxBaseClassDepth = 16;

std::string
LoadResult::ToString() const
{
  if (mError) {
    return *mError;
  }
  std::string result = std::format("loaded {} view{}", mViewCount, mViewCount == 1 ? "" : "s");
  for (const auto &diagnostic : mDiagnostics) {
    result.append("\n");
    result.append(diagnostic.ToString());
  }
  return result;
}

ViewRegistry::ViewRegistry() noexcept : mSerializer(std::make_unique<ViewSerializer>(*this)) {}

ViewRegistry::~ViewRegistry() noexcept = default;

/* static */
ViewRegistry &
ViewRegistry::Get() noexcept
{
  static ViewRegistry sRegistry;
  return sRegistry;
}

void
ViewRegistry::Rebuild() noexcept
{
  ForgetCursors();
  mViews.clear();
  mBoundViews.clear();
  for (const auto &file : mFiles) {
    for (const auto &view : file->mViews) {
      mViews.push_back(&view);
    }
  }
}

LoadResult
ViewRegistry::LoadSource(std::string name, std::string source) noexcept
{
  auto file = ParseViewFile(std::move(name), std::move(source));
  LoadResult result{ .mViewCount = static_cast<u32>(file->mViews.size()), .mDiagnostics = file->mDiagnostics };
  for (const auto &diagnostic : result.mDiagnostics) {
    DBGLOG(core, "[views] {}", diagnostic.ToString());
  }

  // Reloading a file replaces it, but keeps its position (and therefore its precedence).
  auto existing = std::ranges::find_if(mFiles, [&](const auto &f) { return f->mPath == file->mPath; });
  if (existing != mFiles.end()) {
    *existing = std::move(file);
  } else {
    mFiles.push_back(std::move(file));
  }
  Rebuild();
  return result;
}

LoadResult
ViewRegistry::LoadFile(const std::filesystem::path &path) noexcept
{
  std::ifstream stream{ path, std::ios::binary };
  if (!stream) {
    return LoadResult{ .mError = std::format("could not open view file '{}'", path.string()) };
  }
  std::stringstream contents;
  contents << stream.rdbuf();
  return LoadSource(path.string(), contents.str());
}

std::vector<LoadResult>
ViewRegistry::LoadDirectory(const std::filesystem::path &directory) noexcept
{
  std::vector<LoadResult> results;
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) {
    return results;
  }
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator{ directory, error }) {
    if (entry.is_regular_file(error) && entry.path().extension() == ".mdbv") {
      files.push_back(entry.path());
    }
  }
  std::ranges::sort(files);
  for (const auto &file : files) {
    results.push_back(LoadFile(file));
  }
  return results;
}

void
ViewRegistry::Clear() noexcept
{
  mFiles.clear();
  Rebuild();
}

const ViewDefinition *
ViewRegistry::FindDirectMatch(sym::Type *type) noexcept
{
  const std::string name = QualifiedTypeName(type);
  if (name.empty()) {
    return nullptr;
  }
  // Later views take precedence
  for (const ViewDefinition *view : mViews | std::views::reverse) {
    if (MatchesObjectFile(*view, type) && view->mPattern.Matches(name)) {
      return view;
    }
  }
  return nullptr;
}

std::unique_ptr<BoundView>
ViewRegistry::FindAndBind(sym::Type *type, u32 depth) noexcept
{
  if (const ViewDefinition *view = FindDirectMatch(type); view) {
    auto bound = BindView(*view, type, 0);
    if (bound->mBindError) {
      DBGLOG(core, "[views] binding '{}' to {}: {}", view->mPattern.mSource, *type, bound->mBindError->ToString());
    }
    return bound;
  }

  if (depth >= kMaxBaseClassDepth) {
    return nullptr;
  }
  // A view for a base class applies to all classes derived from it. Most derived wins, then declaration order.
  for (const auto &base : DirectBaseClasses(type)) {
    if (!IsViewableType(base.mType)) {
      continue;
    }
    if (auto bound = FindAndBind(base.mType, depth + 1); bound) {
      bound->mRootOffset += base.mOffset;
      return bound;
    }
  }
  return nullptr;
}

const BoundView *
ViewRegistry::Lookup(sym::Type *type) noexcept
{
  if (IsEmpty() || !IsViewableType(type)) {
    return nullptr;
  }
  if (auto it = mBoundViews.find(type); it != mBoundViews.end()) {
    return it->second.get();
  }
  type->ResolveType();
  auto bound = FindAndBind(type, 0);
  if (!bound) {
    // Helps when writing a pattern: this is the name it has to match.
    DBGLOG(core, "[views] no view for '{}'", QualifiedTypeName(type));
  } else if (!bound->mBindError) {
    const ViewDefinition &view = *bound->mDef;
    const std::string through = bound->mMatchedType == type ? std::string{}
                                                            : std::format(" through base class '{}' at offset {}",
                                                                QualifiedTypeName(bound->mMatchedType),
                                                                bound->mRootOffset);
    DBGLOG(core,
      "[views] '{}' uses view '{}' ({}:{}){}",
      QualifiedTypeName(type),
      view.mPattern.mSource,
      view.mFile->mPath,
      view.mLoc.mLine,
      through);
  }
  const BoundView *result = bound.get();
  mBoundViews.emplace(type, std::move(bound));
  return result;
}

sym::DebugAdapterSerializer *
ViewRegistry::SerializerFor(sym::Value &value) noexcept
{
  if (IsEmpty()) {
    return nullptr;
  }
  return Lookup(value.GetType().TypeEntry()) != nullptr ? mSerializer.get() : nullptr;
}

} // namespace mdb::views
