/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <views/bound_ir.h>
#include <views/matcher.h>

// std
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mdb {
class ObjectFile;
}

namespace mdb::views {

struct FrameVariable
{
  std::string_view mName;
  sym::Type *mType;
};

struct BoundFrameTemplate
{
  // Owns the AST and source the bound program points into. Must outlive mCodeObject.
  std::unique_ptr<FrameTemplate> mTemplate;
  std::unique_ptr<CodeObject> mCodeObject;
  // Root i of mCodeObject is variables[mRootVariables[i]] of the BindFrameTemplate call.
  std::vector<u32> mRootVariables;
};

static constexpr u64 kDefaultTextLimit = 4096;
static constexpr u64 kDefaultArrayLimit = 1'000'000;

/**
 * Binds `view` against `matchedType` (the type, or base class of the type, that the view's pattern matched).
 * Always returns a CodeObject; if binding failed it carries the error in `mBindError`.
 */
std::unique_ptr<CodeObject> BindView(const ViewDefinition &view, sym::Type *matchedType, u32 rootOffset) noexcept;
/**
 * Binds a parsed frame template. Always returns a CodeObject. Parse or bind failures are reported in
 * mCodeObject->mBindError, with mRootVariables empty.
 */
BoundFrameTemplate BindFrameTemplate(
  std::unique_ptr<FrameTemplate> tmpl, std::span<const FrameVariable> variables, ObjectFile *objectFile) noexcept;

} // namespace mdb::views
