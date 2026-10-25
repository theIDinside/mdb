/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <views/bound_ir.h>

// std
#include <optional>
#include <string>

namespace mdb::views {

class CodeGen
{
  BoundView &mView;
  std::optional<std::string> mError;

public:
  explicit CodeGen(BoundView &view) noexcept : mView(view) {}

  u32 Emit(Instruction instruction) noexcept;
  void PatchTarget(u32 jump, u32 target) noexcept;
  [[nodiscard]] u32 Here() const noexcept;
  void EmitExpr(NodeId id) noexcept;
  u32 EmitExprFunction(std::string name, NodeId node) noexcept;
  u32 BeginFunction(std::string name, bool canYield) noexcept;
  u32 AddFunction(std::string name, bool canYield) noexcept;
  u32 AddLocal(u32 function, TypeId type) noexcept;
  void EmitFail(std::string message, SourceLoc loc) noexcept;

  [[nodiscard]] const std::optional<std::string> &
  Error() const noexcept
  {
    return mError;
  }
};

} // namespace mdb::views
