/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <symbolication/variable_reference.h>
#include <views/evaluator.h>

// std
#include <unordered_map>
#include <vector>

namespace mdb::views {

/**
 * Where a view's `children` block left off, per variables reference, so that asking for the next page of children
 * doesn't run the block from the start again. A checkpoint is taken every 100 yielded elements.
 *
 * A checkpoint's program state only makes sense with the bound view it was produced by, so all cursors are dropped
 * when the view registry is rebuilt (see ViewRegistry::Generation).
 *
 * Only used from the main thread.
 */
class GlobalCursorCache
{
public:
  struct Checkpoint
  {
    u64 mYieldIndex;
    ProgramState mState;
  };
  struct Cursor
  {
    std::vector<Checkpoint> mCheckpoints;
  };

  static GlobalCursorCache &
  Get() noexcept
  {
    static GlobalCursorCache sCursors;
    return sCursors;
  }

  // The cursor for `id`, created empty if there is none.
  Cursor &
  For(VariableReferenceId id, u64 registryGeneration) noexcept
  {
    if (registryGeneration != mGeneration) {
      mCursors.clear();
      mGeneration = registryGeneration;
    }
    return mCursors[id];
  }

  void
  Clear() noexcept
  {
    mCursors.clear();
  }

private:
  std::unordered_map<VariableReferenceId, Cursor> mCursors;
  u64 mGeneration{ 0 };
};

} // namespace mdb::views
