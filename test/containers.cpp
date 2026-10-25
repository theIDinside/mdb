// Test subject for views (test/views/containers.mdbv, test/driver/views.js).
// MdbTArray mimics Gecko's nsTArray: the elements are stored right after a header that the array points to, and an
// "auto" array keeps its header and first N elements inline.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

// Simulate (very loosely) the hash table in gecko
struct MdbHashTable
{
  uint32_t *mHashes;
  char *mEntries;
  uint32_t mEntrySize;
  uint32_t mCapacity;
  uint32_t mEntryCount; // live entries
};

template <typename K, typename V> struct MdbHashEntry
{
  K mKey;
  V mData;
};

// Open addressing with linear probing, like PLDHashTable. Every slot has a hash: 0 means the slot is free, 1 means
// its entry was removed, anything else is the (adjusted) hash of a live entry. The entries live in a separate
// array of mEntrySize bytes each, so a view has to compute where entry N is.
//
// The hash of a key is simply key + 2 (0 and 1 are taken), and a key goes to slot hash % capacity or, if that is
// used, the next free one. That makes the slot of every entry easy to work out in a test.
template <typename Entry> struct MdbHashMap
{
  static constexpr uint32_t kFree = 0;
  static constexpr uint32_t kRemoved = 1;

  MdbHashTable mTable;

  explicit MdbHashMap(uint32_t capacity)
  {
    mTable.mHashes = static_cast<uint32_t *>(calloc(capacity, sizeof(uint32_t)));
    mTable.mEntries = static_cast<char *>(calloc(capacity, sizeof(Entry)));
    mTable.mEntrySize = sizeof(Entry);
    mTable.mCapacity = capacity;
    mTable.mEntryCount = 0;
  }

  MdbHashMap(const MdbHashMap &) = delete;
  MdbHashMap &operator=(const MdbHashMap &) = delete;

  ~MdbHashMap()
  {
    free(mTable.mHashes);
    free(mTable.mEntries);
  }

  static uint32_t
  Hash(int key)
  {
    return static_cast<uint32_t>(key) + 2;
  }

  Entry *
  EntryAt(uint32_t slot)
  {
    return reinterpret_cast<Entry *>(mTable.mEntries + slot * mTable.mEntrySize);
  }

  // Assumes the key isn't in the table yet, and that the table isn't full.
  void
  Put(int key, int value)
  {
    const uint32_t hash = Hash(key);
    uint32_t slot = hash % mTable.mCapacity;
    while (mTable.mHashes[slot] > kRemoved) {
      slot = (slot + 1) % mTable.mCapacity;
    }
    mTable.mHashes[slot] = hash;
    *EntryAt(slot) = Entry{ key, value };
    ++mTable.mEntryCount;
  }

  void
  Remove(int key)
  {
    const uint32_t hash = Hash(key);
    for (uint32_t slot = hash % mTable.mCapacity; mTable.mHashes[slot] != kFree;
      slot = (slot + 1) % mTable.mCapacity) {
      if (mTable.mHashes[slot] == hash && EntryAt(slot)->mKey == key) {
        mTable.mHashes[slot] = kRemoved;
        --mTable.mEntryCount;
        return;
      }
    }
  }
};

// A singly linked list that ends in a null pointer.
struct MdbListNode
{
  int mValue;
  MdbListNode *mNext;
};

struct MdbList
{
  MdbListNode *mHead;
  uint32_t mLength;
};

struct MdbTreeNode
{
  int mKey;
  int mValue;
  MdbTreeNode *mLeft;
  MdbTreeNode *mRight;
};

struct MdbTree
{
  MdbTreeNode *mRoot;
  uint32_t mSize;
};

// Circular through a sentinel, like mozilla::LinkedList: the list holds a link, elements derive from the link
// type, and the last element points back to the sentinel.
struct MdbLink
{
  MdbLink *mNext;
  MdbLink *mPrev;
};

struct MdbItem : MdbLink
{
  int mId;
};

struct MdbCircularList
{
  MdbLink mSentinel;
};

// The last node points back to the first: following mNext never ends.
struct MdbCorruptList
{
  MdbListNode *mHead;
};

// Its view's children block loops forever.
struct MdbForever
{
  int mUnused;
};

// Links `count` nodes in order, with values from `valueOf(index)`, and returns the first.
template <typename Fn>
static MdbListNode *
LinkNodes(MdbListNode *nodes, int count, Fn valueOf)
{
  for (int i = 0; i < count; ++i) {
    nodes[i].mValue = valueOf(i);
    nodes[i].mNext = i + 1 < count ? &nodes[i + 1] : nullptr;
  }
  return count > 0 ? &nodes[0] : nullptr;
}

struct MdbTArrayHeader
{
  uint32_t mLength;
  uint32_t mCapacity : 31;
  uint32_t mIsAutoArray : 1;
};

static MdbTArrayHeader sEmptyHeader{ 0, 0, 0 };

template <typename T> class MdbTArray_Impl
{
protected:
  MdbTArrayHeader *mHdr{ &sEmptyHeader };

  bool
  UsesHeapBuffer() const
  {
    return mHdr != &sEmptyHeader && !mHdr->mIsAutoArray;
  }

public:
  MdbTArray_Impl() = default;
  MdbTArray_Impl(const MdbTArray_Impl &) = delete;
  MdbTArray_Impl &operator=(const MdbTArray_Impl &) = delete;

  ~MdbTArray_Impl()
  {
    if (UsesHeapBuffer()) {
      free(mHdr);
    }
  }

  T *
  Elements()
  {
    return reinterpret_cast<T *>(mHdr + 1);
  }

  uint32_t
  Length() const
  {
    return mHdr->mLength;
  }

  void
  AppendElement(const T &value)
  {
    if (mHdr == &sEmptyHeader || mHdr->mCapacity < Length() + 1) {
      const uint32_t capacity = Length() < 4 ? 8 : Length() * 2;
      auto *header = static_cast<MdbTArrayHeader *>(malloc(sizeof(MdbTArrayHeader) + sizeof(T) * capacity));
      header->mLength = Length();
      header->mCapacity = capacity;
      header->mIsAutoArray = 0;
      memcpy(header + 1, Elements(), sizeof(T) * Length());
      if (UsesHeapBuffer()) {
        free(mHdr);
      }
      mHdr = header;
    }
    Elements()[mHdr->mLength++] = value;
  }
};

template <typename T> class MdbTArray : public MdbTArray_Impl<T>
{
};

template <typename T, uint32_t N> class MdbAutoTArray : public MdbTArray<T>
{
  MdbTArrayHeader mAutoHeader;
  alignas(T) unsigned char mStorage[sizeof(T) * N];

public:
  MdbAutoTArray()
  {
    mAutoHeader.mLength = 0;
    mAutoHeader.mCapacity = N;
    mAutoHeader.mIsAutoArray = 1;
    this->mHdr = &mAutoHeader;
  }
};

struct Point
{
  int x;
  int y;
};

struct Name
{
  const char *mStr;
  uint32_t mLen;
};

struct MdbBitSet
{
  uint64_t mWords[2];
  uint32_t mCount;
};

struct MdbSetBits
{
  MdbBitSet mBits;
};

struct Flags
{
  uint32_t a : 3;
  uint32_t b : 5;
  int32_t c : 4;
};

// No view of its own: the view for Flags applies.
struct DerivedFlags : Flags
{
  int extra;
};

// Its view refers to a member that doesn't exist.
struct Broken
{
  int mPresent;
};

// Its view's children can't be produced: the pointer is null.
struct NullElements
{
  int *mPtr;
  uint32_t mCount;
};

template <typename T, int N> struct FixedBuffer
{
  T mItems[N];
  int mUsed;
};

namespace outer {
namespace inner {
struct Scoped
{
  int mValue;
};
} // namespace inner
// Inline namespaces are left out of qualified names: this is outer::Versioned
inline namespace v1 {
struct Versioned
{
  int mVersion;
};
} // namespace v1
} // namespace outer

// Its view asks for a template argument it doesn't have.
struct NotATemplate
{
  int x;
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
  MdbTArray<int> empty;
  MdbTArray<int> ints;
  for (int i = 0; i < 1000; ++i) {
    ints.AppendElement(i * 2);
  }

  MdbAutoTArray<Point, 4> points;
  points.AppendElement(Point{ 1, 2 });
  points.AppendElement(Point{ 3, 4 });

  MdbTArray<Name> names;
  names.AppendElement(Name{ "alpha", 5 });
  names.AppendElement(Name{ "beta", 4 });

  MdbBitSet bits{};
  bits.mCount = 70;
  bits.mWords[0] = (uint64_t{ 1 } << 0) | (uint64_t{ 1 } << 3);
  bits.mWords[1] = (uint64_t{ 1 } << 0) | (uint64_t{ 1 } << 5);
  MdbSetBits setBits{ bits };

  Flags flags{ 5, 17, -3 };
  DerivedFlags derived{};
  derived.a = 1;
  derived.b = 2;
  derived.c = -1;
  derived.extra = 42;

  Broken broken{ 1 };
  NullElements nullElements{ nullptr, 3 };

  FixedBuffer<Point, 8> buffer{};
  buffer.mItems[0] = Point{ 5, 6 };
  buffer.mUsed = 1;

  outer::inner::Scoped scoped{ 7 };
  outer::Versioned versioned{ 2 };
  NotATemplate notATemplate{ 1 };

  // hashmap testing. Capacity 8, so key k goes to slot (k + 2) % 8:
  //   10 -> 4, 20 -> 6, 30 -> 0, 40 -> 2, and 18 -> 4, which is taken by 10, so it goes to 5.
  // Removing 20 leaves a removed slot at 6. Live entries in slot order: 30, 40, 10, 18.
  MdbHashMap<MdbHashEntry<int, int>> hashmap{ 8 };
  hashmap.Put(10, 100);
  hashmap.Put(20, 200);
  hashmap.Put(30, 300);
  hashmap.Put(40, 400);
  hashmap.Put(18, 180);
  hashmap.Remove(20);

  // Lists. shortList holds 1, 2, 3, 4, 5. longList holds 0, 3, 6, ... (250 values), for paging.
  MdbListNode shortNodes[5];
  MdbList shortList{ LinkNodes(shortNodes, 5, [](int i) { return i + 1; }), 5 };
  static MdbListNode longNodes[250];
  MdbList longList{ LinkNodes(longNodes, 250, [](int i) { return i * 3; }), 250 };

  // Sentinel list with the items 7, 8, 9.
  MdbItem items[3];
  MdbCircularList circularList;
  MdbLink *previous = &circularList.mSentinel;
  for (int i = 0; i < 3; ++i) {
    items[i].mId = 7 + i;
    items[i].mPrev = previous;
    previous->mNext = &items[i];
    previous = &items[i];
  }
  previous->mNext = &circularList.mSentinel;
  circularList.mSentinel.mPrev = previous;

  // 100, 101, 102, 100, 101, ... forever.
  MdbListNode corruptNodes[3];
  MdbCorruptList corruptList{ LinkNodes(corruptNodes, 3, [](int i) { return 100 + i; }) };
  corruptNodes[2].mNext = &corruptNodes[0];

  MdbForever forever{ 0 };

  // A seven-node BST inserted in the specified order.
  static MdbTreeNode treeNodes[7];
  MdbTreeNode *treeRoot = nullptr;
  const int treeKeys[]{ 50, 30, 70, 20, 40, 60, 80 };
  for (int i = 0; i < 7; ++i) {
    MdbTreeNode &node = treeNodes[i];
    node = MdbTreeNode{ .mKey = treeKeys[i], .mValue = treeKeys[i] * 10, .mLeft = nullptr, .mRight = nullptr };
    MdbTreeNode **link = &treeRoot;
    while (*link != nullptr) {
      link = node.mKey < (*link)->mKey ? &(*link)->mLeft : &(*link)->mRight;
    }
    *link = &node;
  }
  MdbTree tree{ treeRoot, 7 };

  // A balanced tree containing keys 0..299.
  static MdbTreeNode bigTreeNodes[300];
  for (int i = 0; i < 300; ++i) {
    bigTreeNodes[i] = MdbTreeNode{ .mKey = i, .mValue = i * 10, .mLeft = nullptr, .mRight = nullptr };
  }
  const auto linkBalanced = [&](auto &&self, int first, int last) -> MdbTreeNode * {
    if (first >= last) {
      return nullptr;
    }
    const int middle = first + (last - first) / 2;
    MdbTreeNode &node = bigTreeNodes[middle];
    node.mLeft = self(self, first, middle);
    node.mRight = self(self, middle + 1, last);
    return &node;
  };
  MdbTree bigTree{ linkBalanced(linkBalanced, 0, 300), 300 };

  // A 2000-node right chain to exercise the script call depth limit.
  static MdbTreeNode deepTreeNodes[2000];
  for (int i = 0; i < 2000; ++i) {
    deepTreeNodes[i] = MdbTreeNode{
      .mKey = i, .mValue = i * 10, .mLeft = nullptr, .mRight = i + 1 < 2000 ? &deepTreeNodes[i + 1] : nullptr
    };
  }
  MdbTree deepTree{ &deepTreeNodes[0], 2000 };

  // For paging: keys 0..299 in slots 2..301, with every key divisible by 10 removed, which leaves 270 entries.
  MdbHashMap<MdbHashEntry<int, int>> bigHashmap{ 512 };
  for (int key = 0; key < 300; ++key) {
    bigHashmap.Put(key, key * 10);
  }
  for (int key = 0; key < 300; key += 10) {
    bigHashmap.Remove(key);
  }

  use(empty);
  use(ints);
  use(points);
  use(names);
  use(bits);
  use(setBits);
  use(flags);
  use(derived);
  use(broken);
  use(nullElements);
  use(buffer);
  use(scoped);
  use(versioned);
  use(notATemplate);
  use(hashmap);
  use(bigHashmap);
  use(shortList);
  use(longList);
  use(circularList);
  use(corruptList);
  use(forever);
  use(tree);
  use(bigTree);
  use(deepTree);
  return 0; // CONTAINERS_BP
}
