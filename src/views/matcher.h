/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <common/typedefs.h>
#include <views/ast.h>

// std
#include <string>
#include <string_view>
#include <vector>

namespace mdb::sym {
class Type;
}

namespace mdb::views {

struct BaseClass
{
  sym::Type *mType;
  u32 mOffset;
};

// Direct base classes of a struct/class type (canonical types), in declaration order.
std::vector<BaseClass> DirectBaseClasses(sym::Type *type) noexcept;

// The name view patterns are matched against: the fully qualified name of `type`, without inline namespaces and
// with whitespace removed except between two words, e.g. `std::vector<int,std::allocator<int>>`. If the compiler
// didn't put the template arguments in the name (-gsimple-template-names), they're rebuilt from the template
// parameters in the debug info. Empty for anonymous types.
std::string QualifiedTypeName(sym::Type *type) noexcept;

// Does `type` match the `in "objfile"` restriction of `view`, if it has one?
bool MatchesObjectFile(const ViewDefinition &view, sym::Type *type) noexcept;

// Is `type` something views can be applied to?
bool IsViewableType(sym::Type *type) noexcept;

} // namespace mdb::views
