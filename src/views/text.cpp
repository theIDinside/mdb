/** LICENSE TEMPLATE */
#include "text.h"

// std
#include <cstring>
#include <format>

namespace mdb::views {

static constexpr u32 kReplacementCharacter = 0xFFFD;

void
AppendUtf8(u32 cp, std::string &out) noexcept
{
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    cp = kReplacementCharacter;
  }
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

static DecodeResult
DecodeUtf8(std::span<const u8> bytes, bool stopAtTerminator, std::string &out) noexcept
{
  size_t i = 0;
  while (i < bytes.size()) {
    const u8 lead = bytes[i];
    if (lead == 0 && stopAtTerminator) {
      return { .mUnits = i + 1, .mFoundTerminator = true };
    }
    if (lead < 0x80) {
      out.push_back(static_cast<char>(lead));
      ++i;
      continue;
    }

    u32 length = 0;
    u32 cp = 0;
    u32 minimum = 0;
    if ((lead & 0xE0) == 0xC0) {
      length = 2;
      cp = lead & 0x1F;
      minimum = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
      length = 3;
      cp = lead & 0x0F;
      minimum = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
      length = 4;
      cp = lead & 0x07;
      minimum = 0x10000;
    } else {
      AppendUtf8(kReplacementCharacter, out);
      ++i;
      continue;
    }

    u32 consumed = 1;
    bool valid = true;
    for (; consumed < length; ++consumed) {
      if (i + consumed >= bytes.size() || (bytes[i + consumed] & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      cp = (cp << 6) | (bytes[i + consumed] & 0x3F);
    }
    // Overlong encodings and surrogates are invalid too.
    if (!valid || cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      AppendUtf8(kReplacementCharacter, out);
      i += consumed;
      continue;
    }
    AppendUtf8(cp, out);
    i += length;
  }
  return { .mUnits = i, .mFoundTerminator = false };
}

static DecodeResult
DecodeUtf16(std::span<const u8> bytes, bool stopAtTerminator, std::string &out) noexcept
{
  const size_t count = bytes.size() / 2;
  const auto unitAt = [&](size_t index) {
    u16 unit;
    std::memcpy(&unit, bytes.data() + index * 2, 2);
    return unit;
  };

  size_t i = 0;
  while (i < count) {
    const u16 unit = unitAt(i);
    if (unit == 0 && stopAtTerminator) {
      return { .mUnits = i + 1, .mFoundTerminator = true };
    }
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      if (i + 1 < count) {
        const u16 low = unitAt(i + 1);
        if (low >= 0xDC00 && low <= 0xDFFF) {
          AppendUtf8(0x10000 + ((static_cast<u32>(unit) - 0xD800) << 10) + (low - 0xDC00), out);
          i += 2;
          continue;
        }
      }
      AppendUtf8(kReplacementCharacter, out);
      ++i;
      continue;
    }
    // A lone low surrogate decodes to U+FFFD through AppendUtf8
    AppendUtf8(unit, out);
    ++i;
  }
  return { .mUnits = i, .mFoundTerminator = false };
}

static DecodeResult
DecodeUtf32(std::span<const u8> bytes, bool stopAtTerminator, std::string &out) noexcept
{
  const size_t count = bytes.size() / 4;
  for (size_t i = 0; i < count; ++i) {
    u32 unit;
    std::memcpy(&unit, bytes.data() + i * 4, 4);
    if (unit == 0 && stopAtTerminator) {
      return { .mUnits = i + 1, .mFoundTerminator = true };
    }
    AppendUtf8(unit, out);
  }
  return { .mUnits = count, .mFoundTerminator = false };
}

DecodeResult
DecodeText(std::span<const u8> bytes, u8 width, bool stopAtTerminator, std::string &out) noexcept
{
  switch (width) {
  case 2:
    return DecodeUtf16(bytes, stopAtTerminator, out);
  case 4:
    return DecodeUtf32(bytes, stopAtTerminator, out);
  default:
    return DecodeUtf8(bytes, stopAtTerminator, out);
  }
}

void
AppendQuoted(std::string_view utf8, bool truncated, std::string &out) noexcept
{
  out.push_back('"');
  for (const char c : utf8) {
    switch (c) {
    case '"':
      out.append("\\\"");
      break;
    case '\\':
      out.append("\\\\");
      break;
    case '\n':
      out.append("\\n");
      break;
    case '\r':
      out.append("\\r");
      break;
    case '\t':
      out.append("\\t");
      break;
    case '\0':
      out.append("\\0");
      break;
    default:
      if (static_cast<u8>(c) < 0x20 || c == 0x7F) {
        std::format_to(std::back_inserter(out), "\\x{:02x}", static_cast<u8>(c));
      } else {
        out.push_back(c);
      }
    }
  }
  out.push_back('"');
  if (truncated) {
    out.append("...");
  }
}

void
AppendJsonEscaped(std::string_view text, std::string &out) noexcept
{
  for (const char c : text) {
    switch (c) {
    case '"':
      out.append("\\\"");
      break;
    case '\\':
      out.append("\\\\");
      break;
    case '\n':
      out.append("\\n");
      break;
    case '\r':
      out.append("\\r");
      break;
    case '\t':
      out.append("\\t");
      break;
    case '\b':
      out.append("\\b");
      break;
    case '\f':
      out.append("\\f");
      break;
    default:
      if (static_cast<u8>(c) < 0x20) {
        std::format_to(std::back_inserter(out), "\\u{:04x}", static_cast<u8>(c));
      } else {
        out.push_back(c);
      }
    }
  }
}

} // namespace mdb::views
