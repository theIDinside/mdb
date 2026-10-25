/** LICENSE TEMPLATE */
#pragma once

// mdb
#include <common/typedefs.h>
#include <views/diagnostic.h>

// std
#include <string_view>

namespace mdb::views {

enum class TokenKind : u8
{
  Eof,
  Error,
  Identifier,
  Integer,
  Float,
  String,
  Char,
  // The type pattern after `view`: everything up to the next whitespace, e.g. `nsTArray<.*>`
  Pattern,
  // punctuation
  LParen,
  RParen,
  LBrace,
  RBrace,
  LBracket,
  RBracket,
  Comma,
  Semicolon,
  Colon,
  ColonColon,
  Question,
  Dot,
  Ellipsis,
  Arrow,
  Plus,
  PlusPlus,
  PlusEqual,
  Minus,
  MinusMinus,
  MinusEqual,
  Star,
  Slash,
  Percent,
  Amp,
  AmpAmp,
  Pipe,
  PipePipe,
  Caret,
  Tilde,
  Bang,
  Equal,
  EqualEqual,
  BangEqual,
  Less,
  LessEqual,
  LessLess,
  Greater,
  GreaterEqual,
  GreaterGreater,
};

std::string_view TokenKindName(TokenKind kind) noexcept;

struct Token
{
  TokenKind mKind{ TokenKind::Eof };
  // The raw source text of the token. For strings and chars this includes the quotes.
  std::string_view mText;
  SourceLoc mLoc;
  u64 mInteger{ 0 };
  double mFloat{ 0.0 };

  [[nodiscard]] constexpr bool
  Is(TokenKind kind) const noexcept
  {
    return mKind == kind;
  }

  [[nodiscard]] constexpr bool
  IsIdentifier(std::string_view text) const noexcept
  {
    return mKind == TokenKind::Identifier && mText == text;
  }
};

class Lexer
{
  std::string_view mSource;
  u32 mPosition{ 0 };
  SourceLoc mLoc;
  // Nesting of `{}` outside of strings. `view` only starts a view (and is followed by a pattern) at depth 0.
  u32 mBraceDepth{ 0 };
  bool mExpectPattern{ false };

  [[nodiscard]] char Peek(u32 ahead = 0) const noexcept;
  char Advance() noexcept;
  bool SkipWhitespaceAndComments(Token &errorToken) noexcept;
  Token Make(TokenKind kind, u32 start, SourceLoc loc) const noexcept;
  Token LexNumber(u32 start, SourceLoc loc) noexcept;
  Token LexQuoted(char quote, u32 start, SourceLoc loc) noexcept;
  Token LexPattern(u32 start, SourceLoc loc) noexcept;

public:
  // `startLoc` lets a sub-lexer (e.g. for `{expr}` inside a summary template) report positions relative to the
  // enclosing file. Expressions inside a view body are lexed at `braceDepth` 1, where `view` is an ordinary name.
  explicit Lexer(std::string_view source, SourceLoc startLoc = { 1, 1 }, u32 braceDepth = 0) noexcept;
  Token Next() noexcept;
};

} // namespace mdb::views
