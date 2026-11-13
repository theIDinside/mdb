// Test subject for view() calls and dynamic types: an XPCOM-like class hierarchy, built without RTTI.
#include <cstring>

struct ISupports
{
  virtual ~ISupports() = default;
  virtual int
  AddRef()
  {
    return 1;
  }
};

struct IURI : ISupports
{
  virtual const char *Scheme() = 0;
};

struct IFileURL : IURI
{
};

// A second base with its own vtable, so that the IURI in a StandardURL is not at offset 0.
struct ISerializable
{
  virtual ~ISerializable() = default;
  virtual int
  Serialize()
  {
    return 0;
  }
  long mSerialTag = 0x5e;
};

// Stores the whole spec, like nsStandardURL.
class StandardURL final : public ISerializable, public IFileURL
{
public:
  explicit StandardURL(const char *spec) : mSpec(spec), mLength(strlen(spec)) {}
  const char *
  Scheme() override
  {
    return "https";
  }
  const char *mSpec;
  unsigned long mLength;
};

// Builds the spec from its parts, like nsSimpleURI.
class SimpleURI : public IURI
{
public:
  SimpleURI(const char *scheme, const char *path, const char *query)
      : mScheme(scheme), mPath(path), mQuery(query), mHasQuery(query != nullptr)
  {
  }
  const char *
  Scheme() override
  {
    return mScheme;
  }
  const char *mScheme;
  const char *mPath;
  const char *mQuery;
  bool mHasQuery;
};

// Has no view of its own: SimpleURI's view applies through the base class.
class NestedURI final : public SimpleURI
{
public:
  NestedURI() : SimpleURI("view-source", "https://example.com/", nullptr) {}
  int mDepth = 1;
};

// Has no view at all: IURI's spec() is used.
class OpaqueURI final : public IURI
{
public:
  const char *
  Scheme() override
  {
    return "opaque";
  }
  int mHidden = 7;
};

namespace {
class HiddenURI final : public IURI
{
public:
  const char *
  Scheme() override
  {
    return "hidden";
  }
  const char *mWhere = "anonymous";
};
} // namespace

template <typename T> class TemplatedURI final : public IURI
{
public:
  const char *
  Scheme() override
  {
    return "templated";
  }
  T mPort = 8080;
};

struct LoadState
{
  IURI *mURI;
  unsigned mLoadType;
};

struct Point
{
  int x;
  int y;
};

struct Segment
{
  Point a;
  Point b;
};

struct Node
{
  int mValue;
  Node *mNext;
};

// Views of these two call each other, which is a cycle.
struct CycleB;
struct CycleA
{
  CycleB *mB;
};
struct CycleB
{
  CycleA *mA;
};

static volatile long sink = 0;

__attribute__((noinline)) void
use(long v)
{
  sink = sink + v;
}

class DocShell
{
public:
  unsigned mLoadType = 0x40001;

  __attribute__((noinline)) void
  InternalLoad(LoadState *aLoadState)
  {
    use(aLoadState->mLoadType); // DT_LOAD
  }
};

int
main()
{
  StandardURL standard{ "https://example.com/index.html" };
  SimpleURI simple{ "about", "blank", nullptr };
  SimpleURI withQuery{ "data", "text/plain", "charset=utf-8" };
  NestedURI nested;
  OpaqueURI opaque;
  HiddenURI hidden;
  TemplatedURI<short> templated;

  IURI *asInterface = &standard;
  ISupports *asSupports = &standard;
  IURI *noURI = nullptr;

  LoadState standardLoad{ &standard, 1 };
  LoadState simpleLoad{ &simple, 2 };
  LoadState queryLoad{ &withQuery, 3 };
  LoadState nestedLoad{ &nested, 4 };
  LoadState opaqueLoad{ &opaque, 5 };
  LoadState hiddenLoad{ &hidden, 6 };
  LoadState templatedLoad{ &templated, 7 };
  LoadState nullLoad{ nullptr, 8 };

  Segment segment{ { 1, 2 }, { 4, 6 } };
  Node third{ 3, nullptr };
  Node second{ 2, &third };
  Node first{ 1, &second };
  CycleA cycleA{ nullptr };
  CycleB cycleB{ &cycleA };
  cycleA.mB = &cycleB;

  use(asInterface->AddRef() + asSupports->AddRef() + (noURI == nullptr)); // DT_LOCALS

  DocShell docShell;
  docShell.InternalLoad(&standardLoad);
  docShell.InternalLoad(&simpleLoad); // DT_SECOND
  docShell.InternalLoad(&queryLoad);
  docShell.InternalLoad(&nestedLoad);
  docShell.InternalLoad(&opaqueLoad);
  docShell.InternalLoad(&nullLoad);
  return 0; // DT_END
}
