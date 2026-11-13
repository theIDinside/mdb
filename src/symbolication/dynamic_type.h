/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <common/typedefs.h>

// std
#include <optional>
#include <string>
#include <string_view>

namespace mdb {
class ObjectFile;
namespace tc {
class SupervisorState;
}
} // namespace mdb

namespace mdb::sym {

class Type;

// The dynamic (most derived) type of an object, and the address of the complete object.
struct DynamicType
{
  Type *mType;
  u64 mAddress;
};

/**
 * Does `type` (a canonical class type) have a vtable pointer, its own or one of a base class? Only objects of
 * such types have a dynamic type.
 */
bool IsDynamicClass(Type *type) noexcept;

/**
 * The dynamic type of the object of static type `staticType` (a canonical class type) at `address`, following the
 * Itanium C++ ABI: the object's vtable pointer points into the vtable of its dynamic type (`_ZTV<type>`, found in
 * the ELF symbols), and `offset_to_top`, 16 bytes before it, is the offset from the object to the complete object.
 * nullopt if the type has no vtable, the memory can't be read, the vtable has no symbol (a stripped binary, a
 * construction vtable, garbage memory), or the type found doesn't have `staticType` as a base at that offset.
 */
std::optional<DynamicType> ResolveDynamicType(
  tc::SupervisorState &supervisor, Type *staticType, u64 address) noexcept;

/**
 * The class type named `qualifiedName` (as a demangler writes it: `ns::Outer<int>::Inner`) defined in
 * `objectFile`, or nullptr. Names are compared in the form QualifiedTypeName produces.
 */
Type *FindTypeByQualifiedName(ObjectFile &objectFile, std::string_view qualifiedName) noexcept;

// `vtable for ns::Foo` -> `ns::Foo`, from the mangled `_ZTV...` symbol name. Empty if it isn't a vtable symbol.
std::string ClassNameOfVtableSymbol(std::string_view mangled) noexcept;

} // namespace mdb::sym
