/** LICENSE TEMPLATE */
#include "lexer.h"

// std
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <string>

namespace mdb::views {

std::string_view
TokenKindName(TokenKind kind) noexcept
{
  switch (kind) {
  case TokenKind::Eof:
    return "end of input";
  case TokenKind::Error:
    return "invalid token";
  case TokenKind::Identifier:
    return "identifier";
  case TokenKind::Integer:
    return "integer";
  case TokenKind::Float:
    return "float";
  case TokenKind::String:
    return "string";
  case TokenKind::Char:
    return "character";
  case TokenKind::Pattern:
    return "type pattern";
  case TokenKind::LParen:
    return "'('";
  case TokenKind::RParen:
    return "')'";
  case TokenKind::LBrace:
    return "'{'";
  case TokenKind::RBrace:
    return "'}'";
  case TokenKind::LBracket:
    return "'['";
  case TokenKind::RBracket:
    return "']'";
  case TokenKind::Comma:
    return "','";
  case TokenKind::Semicolon:
    return "';'";
  case TokenKind::Colon:
    return "':'";
  case TokenKind::ColonColon:
    return "'::'";
  case TokenKind::Question:
    return "'?'";
  case TokenKind::Dot:
    return "'.'";
  case TokenKind::Ellipsis:
    return "'...'";
  case TokenKind::Arrow:
    return "'->'";
  case TokenKind::Plus:
    return "'+'";
  case TokenKind::PlusPlus:
    return "'++'";
  case TokenKind::PlusEqual:
    return "'+='";
  case TokenKind::Minus:
    return "'-'";
  case TokenKind::MinusMinus:
    return "'--'";
  case TokenKind::MinusEqual:
    return "'-='";
  case TokenKind::Star:
    return "'*'";
  case TokenKind::Slash:
    return "'/'";
  case TokenKind::Percent:
    return "'%'";
  case TokenKind::Amp:
    return "'&'";
  case TokenKind::AmpAmp:
    return "'&&'";
  case TokenKind::Pipe:
    return "'|'";
  case TokenKind::PipePipe:
    return "'||'";
  case TokenKind::Caret:
    return "'^'";
  case TokenKind::Tilde:
    return "'~'";
  case TokenKind::Bang:
    return "'!'";
  case TokenKind::Equal:
    return "'='";
  case TokenKind::EqualEqual:
    return "'=='";
  case TokenKind::BangEqual:
    return "'!='";
  case TokenKind::Less:
    return "'<'";
  case TokenKind::LessEqual:
    return "'<='";
  case TokenKind::LessLess:
    return "'<<'";
  case TokenKind::Greater:
    return "'>'";
  case TokenKind::GreaterEqual:
    return "'>='";
  case TokenKind::GreaterGreater:
    return "'>>'";
  }
  return "token";
}

Lexer::Lexer(std::string_view source, SourceLoc startLoc, u32 braceDepth) noexcept
    : mSource(source), mLoc(startLoc), mBraceDepth(braceDepth)
{
}

char
Lexer::Peek(u32 ahead) const noexcept
{
  const u32 index = mPosition + ahead;
  return index < mSource.size() ? mSource[index] : '\0';
}

char
Lexer::Advance() noexcept
{
  const char c = mSource[mPosition++];
  if (c == '\n') {
    ++mLoc.mLine;
    mLoc.mColumn = 1;
  } else {
    ++mLoc.mColumn;
  }
  return c;
}

Token
Lexer::Make(TokenKind kind, u32 start, SourceLoc loc) const noexcept
{
  return Token{ .mKind = kind, .mText = mSource.substr(start, mPosition - start), .mLoc = loc };
}

bool
Lexer::SkipWhitespaceAndComments(Token &errorToken) noexcept
{
  while (mPosition < mSource.size()) {
    const char c = Peek();
    if (std::isspace(static_cast<unsigned char>(c))) {
      Advance();
    } else if (c == '/' && Peek(1) == '/') {
      while (mPosition < mSource.size() && Peek() != '\n') {
        Advance();
      }
    } else if (c == '/' && Peek(1) == '*') {
      const SourceLoc loc = mLoc;
      const u32 start = mPosition;
      Advance();
      Advance();
      while (mPosition < mSource.size() && !(Peek() == '*' && Peek(1) == '/')) {
        Advance();
      }
      if (mPosition >= mSource.size()) {
        errorToken = Make(TokenKind::Error, start, loc);
        errorToken.mText = "unterminated block comment";
        return false;
      }
      Advance();
      Advance();
    } else {
      break;
    }
  }
  return true;
}

Token
Lexer::LexNumber(u32 start, SourceLoc loc) noexcept
{
  bool isFloat = false;
  int base = 10;
  if (Peek() == '0' && (Peek(1) == 'x' || Peek(1) == 'X')) {
    base = 16;
    Advance();
    Advance();
    while (std::isxdigit(static_cast<unsigned char>(Peek())) || Peek() == '\'') {
      Advance();
    }
  } else if (Peek() == '0' && (Peek(1) == 'b' || Peek(1) == 'B')) {
    base = 2;
    Advance();
    Advance();
    while (Peek() == '0' || Peek() == '1' || Peek() == '\'') {
      Advance();
    }
  } else {
    while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '\'') {
      Advance();
    }
    if (Peek() == '.' && std::isdigit(static_cast<unsigned char>(Peek(1)))) {
      isFloat = true;
      Advance();
      while (std::isdigit(static_cast<unsigned char>(Peek()))) {
        Advance();
      }
    }
    if (Peek() == 'e' || Peek() == 'E') {
      Token token = Make(TokenKind::Error, start, loc);
      token.mText = "exp not supported";
      return token;
    }
  }

  const u32 digitsEnd = mPosition;
  // Integer suffixes (u, l, ul, ull, ...) are accepted and ignored; all integer arithmetic is 64-bit.
  while (!isFloat && (Peek() == 'u' || Peek() == 'U' || Peek() == 'l' || Peek() == 'L')) {
    Advance();
  }
  if (isFloat && (Peek() == 'f' || Peek() == 'F')) {
    Advance();
  }

  Token token = Make(isFloat ? TokenKind::Float : TokenKind::Integer, start, loc);
  std::string digits;
  const u32 digitsStart = start + (base == 10 ? 0 : 2);
  for (u32 i = digitsStart; i < digitsEnd; ++i) {
    if (mSource[i] != '\'') {
      digits.push_back(mSource[i]);
    }
  }

  if (isFloat) {
    token.mFloat = std::strtod(digits.c_str(), nullptr);
    return token;
  }

  const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), token.mInteger, base);
  if (ec != std::errc{} || ptr != digits.data() + digits.size() || digits.empty()) {
    token.mKind = TokenKind::Error;
    token.mText = "invalid integer literal";
  }
  return token;
}

Token
Lexer::LexQuoted(char quote, u32 start, SourceLoc loc) noexcept
{
  Advance(); // opening quote
  while (mPosition < mSource.size() && Peek() != quote) {
    if (Peek() == '\n') {
      break;
    }
    if (Peek() == '\\' && mPosition + 1 < mSource.size()) {
      Advance();
    }
    Advance();
  }
  if (Peek() != quote) {
    Token token = Make(TokenKind::Error, start, loc);
    token.mText = quote == '"' ? "unterminated string literal" : "unterminated character literal";
    return token;
  }
  Advance(); // closing quote
  return Make(quote == '"' ? TokenKind::String : TokenKind::Char, start, loc);
}

Token
Lexer::LexPattern(u32 start, SourceLoc loc) noexcept
{
  // Patterns are regular expressions, so almost anything goes; the pattern simply ends at whitespace.
  while (mPosition < mSource.size() && !std::isspace(static_cast<unsigned char>(Peek()))) {
    Advance();
  }
  return Make(TokenKind::Pattern, start, loc);
}

Token
Lexer::Next() noexcept
{
  Token error;
  if (!SkipWhitespaceAndComments(error)) {
    return error;
  }

  const SourceLoc loc = mLoc;
  const u32 start = mPosition;
  if (mPosition >= mSource.size()) {
    return Make(TokenKind::Eof, start, loc);
  }

  const char c = Peek();
  if (mExpectPattern) {
    mExpectPattern = false;
    // A quoted pattern (for patterns that contain spaces) is lexed as a string.
    if (c != '"') {
      return LexPattern(start, loc);
    }
  }

  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
    while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_' || Peek() == '$') {
      Advance();
    }
    Token token = Make(TokenKind::Identifier, start, loc);
    mExpectPattern = mBraceDepth == 0 && token.mText == "view";
    return token;
  }

  if (std::isdigit(static_cast<unsigned char>(c))) {
    return LexNumber(start, loc);
  }

  if (c == '"' || c == '\'') {
    return LexQuoted(c, start, loc);
  }

  Advance();
  const auto single = [&](TokenKind kind) { return Make(kind, start, loc); };
  const auto pair = [&](char next, TokenKind ifPair, TokenKind otherwise) {
    if (Peek() == next) {
      Advance();
      return Make(ifPair, start, loc);
    }
    return Make(otherwise, start, loc);
  };

  switch (c) {
  case '(':
    return single(TokenKind::LParen);
  case ')':
    return single(TokenKind::RParen);
  case '{':
    ++mBraceDepth;
    return single(TokenKind::LBrace);
  case '}':
    mBraceDepth = mBraceDepth > 0 ? mBraceDepth - 1 : 0;
    return single(TokenKind::RBrace);
  case '[':
    return single(TokenKind::LBracket);
  case ']':
    return single(TokenKind::RBracket);
  case ',':
    return single(TokenKind::Comma);
  case ';':
    return single(TokenKind::Semicolon);
  case '?':
    return single(TokenKind::Question);
  case '~':
    return single(TokenKind::Tilde);
  case '^':
    return single(TokenKind::Caret);
  case '+':
    if (Peek() == '+') {
      Advance();
      return single(TokenKind::PlusPlus);
    }
    return pair('=', TokenKind::PlusEqual, TokenKind::Plus);
  case '*':
    return single(TokenKind::Star);
  case '/':
    return single(TokenKind::Slash);
  case '%':
    return single(TokenKind::Percent);
  case ':':
    return pair(':', TokenKind::ColonColon, TokenKind::Colon);
  case '&':
    return pair('&', TokenKind::AmpAmp, TokenKind::Amp);
  case '|':
    return pair('|', TokenKind::PipePipe, TokenKind::Pipe);
  case '!':
    return pair('=', TokenKind::BangEqual, TokenKind::Bang);
  case '=':
    return pair('=', TokenKind::EqualEqual, TokenKind::Equal);
  case '-':
    if (Peek() == '>') {
      Advance();
      return single(TokenKind::Arrow);
    }
    if (Peek() == '-') {
      Advance();
      return single(TokenKind::MinusMinus);
    }
    return pair('=', TokenKind::MinusEqual, TokenKind::Minus);
  case '.':
    if (Peek() == '.' && Peek(1) == '.') {
      Advance();
      Advance();
      return single(TokenKind::Ellipsis);
    }
    return single(TokenKind::Dot);
  case '<':
    if (Peek() == '<') {
      Advance();
      return single(TokenKind::LessLess);
    }
    return pair('=', TokenKind::LessEqual, TokenKind::Less);
  case '>':
    // `>>` is lexed as a single token; the parser splits it when closing nested template argument lists.
    if (Peek() == '>') {
      Advance();
      return single(TokenKind::GreaterGreater);
    }
    return pair('=', TokenKind::GreaterEqual, TokenKind::Greater);
  default:
    break;
  }

  Token token = Make(TokenKind::Error, start, loc);
  token.mText = "unexpected character";
  return token;
}

} // namespace mdb::views
