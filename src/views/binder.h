/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <views/bound_ir.h>
#include <views/matcher.h>

// std
#include <memory>

namespace mdb::views {

static constexpr u64 kDefaultTextLimit = 4096;
static constexpr u64 kDefaultArrayLimit = 1'000'000;

/**
 * Binds `view` against `matchedType` (the type, or base class of the type, that the view's pattern matched).
 * Always returns a BoundView; if binding failed it carries the error in `mBindError`.
 */
std::unique_ptr<BoundView> BindView(const ViewDefinition &view, sym::Type *matchedType, u32 rootOffset) noexcept;

} // namespace mdb::views
