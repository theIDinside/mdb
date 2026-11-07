#include <cstdio>
#include <vector>

struct Point
{
  int x;
  int y;
};
struct Counter
{
  int mCount = 0;
  const char *mName = "counter";
  void
  bump(int by)
  {
    mCount += by; // LP_METHOD
  }
};

static volatile int sink = 0;
__attribute__((noinline)) void
use(long v)
{
  sink = sink + static_cast<int>(v);
}
__attribute__((noinline)) void
loop(int n)
{
  for (int i = 0; i < n; ++i) {
    // logpoints_opt's DWARF location list puts i in r14/rbx across this call.
    use(i); // LP_LOOP
  }
}
__attribute__((noinline)) int
params(Point *p, Point s, const char *name, char initial)
{
  return p->x + s.y + name[0] + initial; // LP_PARAMS
}
__attribute__((noinline)) void
shadow()
{
  int value = 1;
  {
    int value = 2;
    use(value); // LP_SHADOW_INNER
  }
  use(value); // LP_SHADOW_OUTER
}
__attribute__((noinline)) void
runtimeError()
{
  Point *null = nullptr;
  int ok = 7;
  use(ok); // LP_RUNTIME_ERROR
}
__attribute__((noinline)) void
vectors()
{
  std::vector<int> v{ 1, 2, 3 };
  Point pt{ 5, 6 };
  use(static_cast<long>(v.size()) + pt.x); // LP_AGGREGATE
}
int
main()
{
  loop(5);
  Point a{ 1, 2 };
  Point b{ 3, 4 };
  params(&a, b, "hello", 'h');
  Counter c;
  c.bump(1);
  c.bump(2);
  shadow();
  runtimeError();
  vectors();
  loop(3);
  return 0; // LP_END
}
