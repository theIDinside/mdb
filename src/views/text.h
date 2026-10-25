/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <common/typedefs.h>

// std
#include <span>
#include <string>
#include <string_view>

namespace mdb::views {

// The text parts is slop-coded and left as such intentionally. It's so meh but not trivial but also not important
// for me as a whole.

struct DecodeResult
{
  // Number of code units consumed (including the terminator, if one was found)
  u64 mUnits{ 0 };
  bool mFoundTerminator{ false };
};

/**
 * Decodes `bytes` as a sequence of code units of `width` bytes (1 = UTF-8, 2 = UTF-16, 4 = UTF-32) and appends
 * the result as UTF-8 to `out`. Invalid sequences (bad UTF-8, lone surrogates, out of range code points) decode
 * to U+FFFD. If `stopAtTerminator` is set, decoding stops at the first zero code unit.
 */
DecodeResult DecodeText(std::span<const u8> bytes, u8 width, bool stopAtTerminator, std::string &out) noexcept;

// Appends a code point as UTF-8
void AppendUtf8(u32 codePoint, std::string &out) noexcept;

// Appends `utf8` as a double quoted string for display: quotes, backslashes and control characters are escaped the
// way they'd be written in a C string literal.
void AppendQuoted(std::string_view utf8, bool truncated, std::string &out) noexcept;

// Appends `text` escaped for use inside a JSON string.
void AppendJsonEscaped(std::string_view text, std::string &out) noexcept;

} // namespace mdb::views
