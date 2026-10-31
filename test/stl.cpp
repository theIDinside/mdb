// Test subject for views of standard library types (test/views/stl.mdbv, test/driver/views.js).

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

struct Point
{
  int x;
  int y;
};

template <typename T>
static void
use(T &t)
{
  std::cout << &t << '\n';
}

int
main()
{
  std::vector<int> empty;

  // 1000 elements, with room for more: capacity is the size reserved, not the size.
  std::vector<int> ints;
  ints.reserve(1024);
  for (int i = 0; i < 1000; ++i) {
    ints.push_back(i * 2);
  }

  std::vector<Point> points{ { 1, 2 }, { 3, 4 } };

  // Elements with a view of their own.
  std::vector<std::vector<int>> nested{ { 1 }, { 2, 3 }, {} };
  std::vector<std::array<Point, 4>> arraysOfPoints{
    { Point{ .x = 1, .y = 2 }, { .x = 3, .y = 4 }, { .x = 5, .y = 6 }, { .x = 7, .y = 8 } },
    { Point{ .x = 10, .y = 20 }, { .x = 30, .y = 40 }, { .x = 50, .y = 60 }, { .x = 70, .y = 80 } },
    { Point{ .x = 100, .y = 200 }, { .x = 300, .y = 400 }, { .x = 500, .y = 600 }, { .x = 700, .y = 800 } },
  };

  // A bit per element, packed in 64-bit words: bits 0, 3, 64 and 69 of 70 are set.
  std::vector<bool> bools(70, false);
  bools[0] = true;
  bools[3] = true;
  bools[64] = true;
  bools[69] = true;

  // Short strings live in the inline buffer, long ones on the heap.
  std::string emptyString;
  std::string shortString{ "short" };
  std::string longString{ "a string that is too long for the inline buffer" };
  std::u16string utf16String{ u"日本語" };
  // const -> typedef std::string -> basic_string: the typedef is under the qualifier.
  const std::string constString{ "const" };
  std::vector<std::string> strings{ "one", "two" };

  // A view of part of a string: not NUL-terminated, so only its length is shown.
  std::string_view hello{ longString.data() + 2, 6 };
  std::string_view emptyView;

  use(empty);
  use(ints);
  use(points);
  use(nested);
  use(bools);
  use(emptyString);
  use(shortString);
  use(utf16String);
  use(constString);
  use(strings);
  use(hello);
  use(emptyView);
  return 0; // STL_BP
}
