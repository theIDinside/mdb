/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <symbolication/value_visualizer.h>
#include <utils/smartptr.h>

// std
#include <vector>

namespace mdb {
struct VariableContext;
}

namespace mdb::sym {
class Value;
}

namespace mdb::views {

// The children of `value` according to its view. Returns true if a view decided the children (`result` may then
// still be empty, e.g. for `children none`), false if the caller should fall back to the type's own members.
bool ResolveUsingView(const VariableContext &context,
  sym::Value &value,
  sym::ValueRange range,
  std::vector<Ref<sym::Value>> &result) noexcept;

} // namespace mdb::views
