// Test subject for string resolver development and testing.
// Defines custom UTF-8 and UTF-16 string types modelled after Firefox's nsString/nsCString architecture,
// where internal data is stored as char (UTF-8) or char16_t (UTF-16) with explicit length and capacity fields.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

// Flags mimicking Firefox's nsString flags
enum StringFlags : uint32_t
{
  // Data is owned by this object and must be freed
  DataOwned = 0x1,
  // Data points to a string literal (read-only, not freed)
  DataLiteral = 0x2,
};

// Firefox-style UTF-8 string (nsCString analogue)
struct MdbCString
{
  char *mData;
  uint32_t mLength;
  uint32_t mCapacity;
  uint32_t mFlags;

  MdbCString() : mData{ nullptr }, mLength{ 0 }, mCapacity{ 0 }, mFlags{ 0 } {}

  explicit MdbCString(const char *literal)
      : mData{ const_cast<char *>(literal) }, mLength{ static_cast<uint32_t>(strlen(literal)) },
        mCapacity{ mLength }, mFlags{ DataLiteral }
  {
  }

  MdbCString(const char *src, uint32_t len) : mLength{ len }, mCapacity{ len + 1 }, mFlags{ DataOwned }
  {
    mData = static_cast<char *>(malloc(mCapacity));
    memcpy(mData, src, len);
    mData[len] = '\0';
  }

  ~MdbCString()
  {
    if (mFlags & DataOwned) {
      free(mData);
    }
  }

  MdbCString(MdbCString &&other) noexcept
      : mData{ other.mData }, mLength{ other.mLength }, mCapacity{ other.mCapacity }, mFlags{ other.mFlags }
  {
    other.mData = nullptr;
    other.mLength = 0;
    other.mCapacity = 0;
    other.mFlags = 0;
  }

  MdbCString(const MdbCString &) = delete;
  MdbCString &operator=(const MdbCString &) = delete;
};

// Firefox-style UTF-16 string (nsString analogue)
struct MdbString
{
  char16_t *mData;
  uint32_t mLength;
  uint32_t mCapacity;
  uint32_t mFlags;

  MdbString() : mData{ nullptr }, mLength{ 0 }, mCapacity{ 0 }, mFlags{ 0 } {}

  explicit MdbString(const char16_t *literal)
      : mData{ const_cast<char16_t *>(literal) }, mLength{ 0 }, mCapacity{ 0 }, mFlags{ DataLiteral }
  {
    const char16_t *p = literal;
    while (*p++) {
      ++mLength;
    }
    mCapacity = mLength;
  }

  MdbString(const char16_t *src, uint32_t len) : mLength{ len }, mCapacity{ len + 1 }, mFlags{ DataOwned }
  {
    mData = static_cast<char16_t *>(malloc(mCapacity * sizeof(char16_t)));
    memcpy(mData, src, len * sizeof(char16_t));
    mData[len] = u'\0';
  }

  MdbString(MdbString &&other) noexcept
      : mData{ other.mData }, mLength{ other.mLength }, mCapacity{ other.mCapacity }, mFlags{ other.mFlags }
  {
    other.mData = nullptr;
    other.mLength = 0;
    other.mCapacity = 0;
    other.mFlags = 0;
  }

  ~MdbString()
  {
    if (mFlags & DataOwned) {
      free(mData);
    }
  }

  MdbString(const MdbString &) = delete;
  MdbString &operator=(const MdbString &) = delete;
};

// Represents a pair of equivalent strings in both encodings, as Firefox
// frequently holds both representations simultaneously when bridging JS and native code.
struct StringPair
{
  MdbCString utf8;
  MdbString utf16;
};

// Naive UTF-8 -> UTF-16 conversion (ASCII + basic BMP only, sufficient for test data)
static MdbString
utf8_to_utf16(const MdbCString &src)
{
  // Allocate worst-case: each UTF-8 byte could expand to one UTF-16 code unit
  uint32_t maxLen = src.mLength;
  char16_t *buf = static_cast<char16_t *>(malloc((maxLen + 1) * sizeof(char16_t)));

  const unsigned char *in = reinterpret_cast<const unsigned char *>(src.mData);
  const unsigned char *end = in + src.mLength;
  char16_t *out = buf;

  while (in < end) {
    uint32_t codepoint;
    unsigned char b = *in;
    if (b < 0x80) {
      codepoint = b;
      ++in;
    } else if ((b & 0xE0) == 0xC0) {
      codepoint = (b & 0x1F) << 6;
      codepoint |= (in[1] & 0x3F);
      in += 2;
    } else if ((b & 0xF0) == 0xE0) {
      codepoint = (b & 0x0F) << 12;
      codepoint |= (in[1] & 0x3F) << 6;
      codepoint |= (in[2] & 0x3F);
      in += 3;
    } else {
      // 4-byte sequence: encode as surrogate pair
      codepoint = (b & 0x07) << 18;
      codepoint |= (in[1] & 0x3F) << 12;
      codepoint |= (in[2] & 0x3F) << 6;
      codepoint |= (in[3] & 0x3F);
      in += 4;
      codepoint -= 0x10000;
      *out++ = static_cast<char16_t>(0xD800 | (codepoint >> 10));
      *out++ = static_cast<char16_t>(0xDC00 | (codepoint & 0x3FF));
      continue;
    }
    *out++ = static_cast<char16_t>(codepoint);
  }
  *out = u'\0';
  uint32_t len = static_cast<uint32_t>(out - buf);
  MdbString result{ buf, len };
  free(buf);
  return result;
}

// Naive UTF-16 -> UTF-8 conversion
static MdbCString
utf16_to_utf8(const MdbString &src)
{
  // Worst case: 4 UTF-8 bytes per UTF-16 code unit
  uint32_t maxBytes = src.mLength * 4;
  char *buf = static_cast<char *>(malloc(maxBytes + 1));

  const char16_t *in = src.mData;
  const char16_t *end = in + src.mLength;
  char *out = buf;

  while (in < end) {
    uint32_t codepoint = *in++;
    if (codepoint >= 0xD800 && codepoint <= 0xDBFF && in < end) {
      uint32_t low = *in++;
      codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
    }

    if (codepoint < 0x80) {
      *out++ = static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
      *out++ = static_cast<char>(0xC0 | (codepoint >> 6));
      *out++ = static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
      *out++ = static_cast<char>(0xE0 | (codepoint >> 12));
      *out++ = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
      *out++ = static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
      *out++ = static_cast<char>(0xF0 | (codepoint >> 18));
      *out++ = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
      *out++ = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
      *out++ = static_cast<char>(0x80 | (codepoint & 0x3F));
    }
  }
  *out = '\0';
  uint32_t len = static_cast<uint32_t>(out - buf);
  MdbCString result{ buf, len };
  free(buf);
  return result;
}

static void
print(const MdbCString &s)
{
  if (s.mData && s.mLength > 0) {
    std::cout.write(s.mData, s.mLength);
  }
  std::cout << '\n';
}

static void
print(const MdbString &s)
{
  MdbCString utf8 = utf16_to_utf8(s);
  if (utf8.mData && utf8.mLength > 0) {
    std::cout.write(utf8.mData, utf8.mLength);
  }
  std::cout << '\n';
}

void
literal_strings()
{
  // DataLiteral flagged strings — data pointer points into read-only segment
  MdbCString utf8_literal{ "Hello, World!" };
  MdbString utf16_literal{ u"Hello, World!" };

  MdbCString ascii_path{ "/home/user/documents/report.pdf" };
  MdbString wide_path{ u"/home/user/documents/report.pdf" };

  print(utf8_literal);
  print(utf16_literal);
  print(ascii_path);
  print(wide_path);
  int bp = 0; // LITERAL_STRINGS_BP
  (void)bp;
}

void
heap_strings()
{
  // DataOwned flagged strings — data is heap-allocated
  const char src_utf8[] = "The quick brown fox jumps over the lazy dog";
  MdbCString owned_utf8{ src_utf8, sizeof(src_utf8) - 1 };

  const char16_t src_utf16[] = u"The quick brown fox jumps over the lazy dog";
  uint32_t u16len = (sizeof(src_utf16) / sizeof(char16_t)) - 1;
  MdbString owned_utf16{ src_utf16, u16len };

  print(owned_utf8);
  print(owned_utf16);
  int bp = 0; // HEAP_STRINGS_BP
  (void)bp;
}

void
small_strings()
{
  // Small heap-allocated strings (previously tested inline/SBO path)
  MdbString short_str{ u"Hi", 2 };
  MdbString medium_str{ u"fifteen chars!!", 15 };
  MdbString slightly_longer{ u"sixteen chars!!!", 16 };

  print(short_str);
  print(medium_str);
  print(slightly_longer);
  int bp = 0; // SMALL_STRINGS_BP
  (void)bp;
}

void
unicode_strings()
{
  // Non-ASCII content: emoji, CJK, combining characters
  MdbCString emoji_utf8{ "Hello \xF0\x9F\x98\x80 World" };       // U+1F600 GRINNING FACE
  MdbCString cjk_utf8{ "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E" }; // 日本語
  MdbCString latin_ext{ "\xC3\xA9\xC3\xA0\xC3\xBC" };            // éàü

  MdbString emoji_utf16{ u"Hello \U0001F600 World" };
  MdbString cjk_utf16{ u"\u65E5\u672C\u8A9E" };       // 日本語
  MdbString latin_ext_utf16{ u"\u00E9\u00E0\u00FC" }; // éàü

  print(emoji_utf8);
  print(cjk_utf8);
  print(latin_ext);
  print(emoji_utf16);
  print(cjk_utf16);
  print(latin_ext_utf16);
  int bp = 0; // UNICODE_STRINGS_BP
  (void)bp;
}

void
conversions()
{
  // Round-trip: UTF-8 -> UTF-16 -> UTF-8
  MdbCString original_utf8{ "Caf\xC3\xA9" }; // "Café"
  MdbString as_utf16 = utf8_to_utf16(original_utf8);
  MdbCString back_to_utf8 = utf16_to_utf8(as_utf16);

  // UTF-16 -> UTF-8 -> UTF-16
  MdbString original_utf16{ u"\u65E5\u672C\u8A9E" }; // 日本語
  MdbCString as_utf8 = utf16_to_utf8(original_utf16);
  MdbString back_to_utf16 = utf8_to_utf16(as_utf8);

  print(original_utf8);
  print(as_utf16);
  print(back_to_utf8);
  print(original_utf16);
  print(as_utf8);
  print(back_to_utf16);
  int bp = 0; // CONVERSIONS_BP
  (void)bp;
}

void
string_pairs()
{
  // StringPair holds both encodings simultaneously
  StringPair greeting{ MdbCString{ "Hello, Debugger!" }, MdbString{ u"Hello, Debugger!" } };
  StringPair japanese{ MdbCString{ "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF" },
    MdbString{ u"\u3053\u3093\u306B\u3061\u306F" } }; // こんにちは

  print(greeting.utf8);
  print(greeting.utf16);
  print(japanese.utf8);
  print(japanese.utf16);
  int bp = 0; // STRING_PAIRS_BP
  (void)bp;
}

void
empty_and_null()
{
  MdbCString empty_cstr{};
  MdbString empty_str{};

  MdbCString empty_owned{ "", 0 };
  MdbString empty_owned_utf16{ u"", 0 };

  print(empty_cstr);
  print(empty_str);
  print(empty_owned);
  print(empty_owned_utf16);
  int bp = 0; // EMPTY_NULL_BP
  (void)bp;
}

int
main()
{
  literal_strings();
  heap_strings();
  small_strings();
  unicode_strings();
  conversions();
  string_pairs();
  empty_and_null();
  return 0; // MAIN_END_BP
}
