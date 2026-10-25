/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <symbolication/value_visualizer.h>

namespace mdb::views {

class ViewRegistry;

// Serializes values that have a view: the summary becomes the `value` field, and the view decides whether the
// value is expandable and how many indexed children it has.
class ViewSerializer final : public sym::DebugAdapterSerializer
{
  ViewRegistry &mRegistry;

public:
  explicit ViewSerializer(ViewRegistry &registry) noexcept : mRegistry(registry) {}

  std::optional<std::pmr::string> SerializeVariable(
    const sym::Value &value, std::pmr::memory_resource *allocator) noexcept final;

  [[nodiscard]] std::string_view
  SerializerType() const override
  {
    return "ViewSerializer";
  }
};

// Serializes the "child" that reports why a view's children could not be produced. The message is the value's
// name.
class ViewErrorSerializer final : public sym::DebugAdapterSerializer
{
public:
  std::optional<std::pmr::string> SerializeVariable(
    const sym::Value &value, std::pmr::memory_resource *allocator) noexcept final;

  [[nodiscard]] std::string_view
  SerializerType() const override
  {
    return "ViewErrorSerializer";
  }
};

} // namespace mdb::views
