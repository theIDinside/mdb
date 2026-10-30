/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <common.h>
#include <symbolication/value_visualizer.h>
#include <utils/smartptr.h>
#include <views/ast.h>
#include <views/bound_ir.h>
#include <views/evaluator.h>
#include <views/matcher.h>

// std
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mdb::sym {
class Type;
class Value;
} // namespace mdb::sym

namespace mdb::views {

class ViewSerializer;

struct LoadResult
{
  u32 mViewCount{ 0 };
  // Parse errors. Views with errors are skipped, the rest of the file is loaded.
  std::vector<Diagnostic> mDiagnostics;
  // Set if the file couldn't be read at all.
  std::optional<std::string> mError;

  [[nodiscard]] std::string ToString() const;
};

/**
 * Owns all loaded view files and knows which view applies to which type. Matching is lazy: the first time a value
 * of some type is displayed, the view patterns are matched against its qualified name (see QualifiedTypeName), and
 * then against the names of its base classes. The result, including "no view", is remembered per type.
 *
 * Only used from the main thread.
 */
class ViewRegistry
{
  std::vector<std::unique_ptr<ViewFile>> mFiles;
  // All views, in load order.
  std::vector<const ViewDefinition *> mViews;
  // nullptr = this type has no view
  std::unordered_map<sym::Type *, std::unique_ptr<BoundView>> mBoundViews;
  std::unique_ptr<ViewSerializer> mSerializer;
  // Bumped whenever the set of views changes and all bound views are thrown away.
  u64 mGeneration{ 0 };

  ViewRegistry() noexcept;

  void Rebuild() noexcept;
  const ViewDefinition *FindDirectMatch(sym::Type *type) noexcept;
  std::unique_ptr<BoundView> FindAndBind(sym::Type *type, u32 depth) noexcept;

public:
  using OnProcessedView = std::function<void(const fs::path &, const LoadResult &)>;
  ~ViewRegistry() noexcept;
  static ViewRegistry &Get() noexcept;

  // Load (or reload, if a file with the same path was loaded before) a view file. Views in files loaded later take
  // precedence over earlier ones.
  LoadResult LoadFile(const fs::path &path) noexcept;
  LoadResult LoadSource(std::string name, std::string source) noexcept;
  // Load all `*.mdbv` files in a directory, in name order.
  std::vector<LoadResult> LoadDirectory(const fs::path &directory, const OnProcessedView &onProcessed) noexcept;
  void Clear() noexcept;

  // Changes every time the loaded views change. Anything derived from a BoundView must not outlive its generation.
  [[nodiscard]] u64
  Generation() const noexcept
  {
    return mGeneration;
  }

  [[nodiscard]] bool
  IsEmpty() const noexcept
  {
    return mViews.empty();
  }

  // The view for values of `type` (a canonical type), or nullptr.
  const BoundView *Lookup(sym::Type *type) noexcept;

  // The serializer for `value` if a view applies to it, otherwise nullptr.
  sym::DebugAdapterSerializer *SerializerFor(sym::Value &value) noexcept;
};

} // namespace mdb::views
