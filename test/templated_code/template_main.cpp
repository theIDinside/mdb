#include "template.h"
#include "template_foo.h"
#include <memory>

template <typename T, size_t Capacity> class StaticVector;

// Outside the class, after StaticVector definition
template <typename First, typename... Rest>
constexpr auto
ToVector(First &&first, Rest &&...rest) noexcept
{
  return StaticVector<First, sizeof...(Rest) + 1>::ToFilledVector(
    std::forward<First>(first), std::forward<Rest>(rest)...);
}

template <typename T, size_t Capacity> class StaticVector
{
  T mElements[Capacity];
  size_t mSize{ 0 };

  constexpr void
  InsertBack(T &&t) noexcept
  {
    mElements[mSize] = std::move(t);
    ++mSize;
  }

public:
  [[nodiscard]] constexpr bool
  IsEmpty() const noexcept
  {
    return mSize == 0;
  }

  constexpr T *
  Get(size_t index) const noexcept
  {
    if (index >= mSize) {
      return nullptr;
    }
    return &mElements[index];
  }

  constexpr void
  Pop() noexcept
  {
    if (mSize == 0) {
      return;
    }

    --mSize;
    std::destroy_at(mElements + mSize);
  }

  constexpr bool
  PushBack(const T &el) noexcept
  {
    if (mSize >= Capacity) {
      return false;
    }

    InsertBack(el);
    return true;
  }

  constexpr bool
  PushBack(T &&el) noexcept
  {
    if (mSize >= Capacity) {
      return false;
    }

    InsertBack(std::forward<T>(el));
    return true;
  }

  template <typename... Args>
  constexpr bool
  EmplaceBack(Args &&...args) noexcept
  {
    if (mSize >= Capacity) {
      return false;
    }

    std::construct_at(mElements + mSize, std::forward<Args>(args)...);
    ++mSize;
    return true;
  }

  template <typename First, typename... Rest>
  static constexpr auto
  ToFilledVector(First &&first, Rest &&...rest) noexcept -> StaticVector<First, sizeof...(Rest) + 1>
  {
    using ElementType = std::common_type_t<std::remove_cvref_t<First>, std::remove_cvref_t<Rest>...>;
    StaticVector<ElementType, sizeof...(Rest) + 1> vec;
    vec.InsertBack(std::forward<First>(first));
    (vec.InsertBack(std::forward<Rest>(rest)), ...);
    return vec;
  }

  static constexpr auto
  ListToVector(std::initializer_list<T> initializerList) noexcept
  {
    StaticVector<T, initializerList.size()> vec{};
    for (auto &&el : initializerList) {
      vec.InsertBack(std::move(el));
    }
    return vec;
  }

  constexpr auto
  begin(this auto &&self) noexcept
  {
    return self.mElements;
  }

  constexpr auto
  end(this auto &&self) noexcept
  {
    return self.mElements + self.mSize;
  }
};

template <typename First, typename... Rest>
StaticVector(First, Rest...)
  -> StaticVector<std::common_type_t<std::remove_cvref_t<First>, std::remove_cvref_t<Rest>...>,
    sizeof...(Rest) + 1>;

int
main(int argc, const char **argv)
{
  int a = 10;
  int b = 20;

  auto stringVector = ToVector(std::string{ "one" }, std::string{ "two" }, std::string{ "three" });
  auto intVector = ToVector<int>(1, 2, 3);

  StaticVector<float, 5> floatVec;
  floatVec.PushBack(1.23);
  floatVec.PushBack(3.14);

  const auto a_lt_b = less_than(a, b);
  const auto a_gt_b = greater_than(a, b);
  const auto a_eq_b = equals(a, b);

  // For future tests that involves type parsing
  TemplateType<int> a_and_b{ 10, 20 };
  TemplateType<float> float_a_and_b{ 10, 20 };
  TestObject obj{ 30, 40 };

  TemplateType<int> a_and_b_equal{ 10 };

  const auto lt = obj._less_than();
  const auto gt = obj._greater_than();
  const auto eq = obj._equals();
}