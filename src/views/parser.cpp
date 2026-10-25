/** LICENSE TEMPLATE */
#include "parser.h"

// mdb
#include <views/lexer.h>

// std
#include <algorithm>
#include <array>
#include <format>

namespace mdb::views {

using namespace std::string_view_literals;

static constexpr auto kBuiltinTypeKeywords = std::to_array<std::string_view>({
  "void",
  "bool",
  "char",
  "char8_t",
  "char16_t",
  "char32_t",
  "wchar_t",
  "short",
  "int",
  "long",
  "signed",
  "unsigned",
  "float",
  "double",
  "const",
  "volatile",
  "u8",
  "u16",
  "u32",
  "u64",
  "i8",
  "i16",
  "i32",
  "i64",
  "f32",
  "f64",
  "size_t",
  "ssize_t",
  "uintptr_t",
  "intptr_t",
  "ptrdiff_t",
  "int8_t",
  "int16_t",
  "int32_t",
  "int64_t",
  "uint8_t",
  "uint16_t",
  "uint32_t",
  "uint64_t",
});

bool
IsBuiltinTypeKeyword(std::string_view word) noexcept
{
  return std::ranges::find(kBuiltinTypeKeywords, word) != kBuiltinTypeKeywords.end();
}

std::string_view
ToString(BinaryOp op) noexcept
{
  switch (op) {
  case BinaryOp::Add:
    return "+";
  case BinaryOp::Sub:
    return "-";
  case BinaryOp::Mul:
    return "*";
  case BinaryOp::Div:
    return "/";
  case BinaryOp::Mod:
    return "%";
  case BinaryOp::Shl:
    return "<<";
  case BinaryOp::Shr:
    return ">>";
  case BinaryOp::BitAnd:
    return "&";
  case BinaryOp::BitOr:
    return "|";
  case BinaryOp::BitXor:
    return "^";
  case BinaryOp::Equal:
    return "==";
  case BinaryOp::NotEqual:
    return "!=";
  case BinaryOp::Less:
    return "<";
  case BinaryOp::LessEqual:
    return "<=";
  case BinaryOp::Greater:
    return ">";
  case BinaryOp::GreaterEqual:
    return ">=";
  case BinaryOp::LogicalAnd:
    return "&&";
  case BinaryOp::LogicalOr:
    return "||";
  }
  return "?";
}

std::string_view
ToString(UnaryOp op) noexcept
{
  switch (op) {
  case UnaryOp::Plus:
    return "+";
  case UnaryOp::Negate:
    return "-";
  case UnaryOp::LogicalNot:
    return "!";
  case UnaryOp::BitNot:
    return "~";
  case UnaryOp::Deref:
    return "*";
  case UnaryOp::AddressOf:
    return "&";
  }
  return "?";
}

static std::string
JoinQualified(std::span<const std::string_view> parts, std::string_view separator, bool rooted)
{
  std::string result = rooted ? "::" : "";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      result.append(separator);
    }
    result.append(parts[i]);
  }
  return result;
}

std::string
TypeExpr::BaseSpelling() const
{
  return JoinQualified(mNameParts, mIsBuiltin ? " " : "::", mIsRooted);
}

std::string
TypeExpr::Spelling() const
{
  std::string result = BaseSpelling();
  if (mPointerDepth > 0) {
    result.push_back(' ');
    result.append(mPointerDepth, '*');
  }
  return result;
}

namespace {

// Processes escape sequences of a string or char literal body (without the quotes). Returns an error message on
// failure.
std::optional<std::string>
Unescape(std::string_view body, std::string &out)
{
  const auto appendUtf8 = [&out](u32 cp) {
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
  };

  const auto hexValue = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return -1;
  };

  for (size_t i = 0; i < body.size(); ++i) {
    const char c = body[i];
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (++i >= body.size()) {
      return "trailing backslash in literal";
    }
    switch (body[i]) {
    case 'n':
      out.push_back('\n');
      break;
    case 't':
      out.push_back('\t');
      break;
    case 'r':
      out.push_back('\r');
      break;
    case '0':
      out.push_back('\0');
      break;
    case '\\':
      out.push_back('\\');
      break;
    case '"':
      out.push_back('"');
      break;
    case '\'':
      out.push_back('\'');
      break;
    case '{':
      out.push_back('{');
      break;
    case '}':
      out.push_back('}');
      break;
    case 'x':
    case 'u':
    case 'U': {
      const size_t digits = body[i] == 'x' ? 2 : (body[i] == 'u' ? 4 : 8);
      u32 value = 0;
      for (size_t d = 0; d < digits; ++d) {
        if (i + 1 >= body.size() || hexValue(body[i + 1]) < 0) {
          return std::format("expected {} hex digits in escape sequence", digits);
        }
        value = (value << 4) | static_cast<u32>(hexValue(body[++i]));
      }
      if (digits == 2) {
        out.push_back(static_cast<char>(value));
      } else {
        appendUtf8(value);
      }
      break;
    }
    default:
      return std::format("unknown escape sequence '\\{}'", body[i]);
    }
  }
  return std::nullopt;
}

class Parser
{
  ViewFile &mFile;
  std::vector<Token> mTokens;
  u32 mPos{ 0 };
  std::optional<Diagnostic> mError;
  // Names declared with `using` so far in the view being parsed; used to recognize casts like `(T*)p`.
  std::vector<std::string_view> mTypeNames;

  [[nodiscard]] bool
  IsTypeName(std::string_view name) const noexcept
  {
    return std::ranges::find(mTypeNames, name) != mTypeNames.end();
  }

public:
  Parser(ViewFile &file, std::vector<Token> tokens) noexcept : mFile(file), mTokens(std::move(tokens)) {}

  // Parse all tokens of a file
  void ParseFile() noexcept;

  // Parse a standalone expression (used for `{expr}` inside summary templates).
  ExprId ParseStandaloneExpression(FormatSpec &format, std::span<const std::string_view> typeNames) noexcept;

  std::optional<Diagnostic>
  TakeError() noexcept
  {
    auto err = std::move(mError);
    mError.reset();
    return err;
  }

private:
  [[nodiscard]] const Token &
  Peek(u32 ahead = 0) const noexcept
  {
    const u32 index = std::min<u32>(mPos + ahead, mTokens.size() - 1);
    return mTokens[index];
  }

  const Token &
  Advance() noexcept
  {
    const Token &token = mTokens[mPos];
    if (mPos + 1 < mTokens.size()) {
      ++mPos;
    }
    return token;
  }

  bool
  Accept(TokenKind kind) noexcept
  {
    if (Peek().Is(kind)) {
      Advance();
      return true;
    }
    return false;
  }

  bool
  AcceptIdentifier(std::string_view text) noexcept
  {
    if (Peek().IsIdentifier(text)) {
      Advance();
      return true;
    }
    return false;
  }

  [[nodiscard]] bool
  Failed() const noexcept
  {
    return mError.has_value();
  }

  void
  Error(const Token &at, std::string message) noexcept
  {
    if (mError) {
      return;
    }
    if (at.Is(TokenKind::Error)) {
      message = std::string{ at.mText };
    }
    mError = Diagnostic{ .mFile = mFile.mPath, .mLoc = at.mLoc, .mMessage = std::move(message) };
  }

  bool
  Expect(TokenKind kind, std::string_view context) noexcept
  {
    if (Accept(kind)) {
      return true;
    }
    Error(Peek(), std::format("expected {} {}, found '{}'", TokenKindName(kind), context, Peek().mText));
    return false;
  }

  ExprId
  AddNewExpression(ExprKind kind, SourceLoc loc) noexcept
  {
    mFile.mArena.mExprs.push_back(Expr{ .mKind = kind, .mLoc = loc });
    return static_cast<ExprId>(mFile.mArena.mExprs.size() - 1);
  }

  Expr &
  Get(ExprId id) noexcept
  {
    return mFile.mArena.mExprs[id];
  }

  StmtId
  AddStatement(Stmt stmt) noexcept
  {
    mFile.mArena.mStmts.push_back(std::move(stmt));
    return static_cast<StmtId>(mFile.mArena.mStmts.size() - 1);
  }

  void ParseView() noexcept;
  void SkipView(u32 viewStart) noexcept;
  bool ParsePattern(TypePattern &pattern) noexcept;
  bool ParseUsing(ViewDefinition &view) noexcept;
  bool IsDeclared(const ViewDefinition &view, std::string_view name) const noexcept;
  bool ParseViewBody(ViewDefinition &view) noexcept;
  StmtId ParseBlock() noexcept;
  StmtId ParseStatement() noexcept;
  StmtId ParseSimpleStatement() noexcept;
  StmtId ParseDeclaration() noexcept;
  StmtId ParseAssignmentOrIncrement() noexcept;
  [[nodiscard]] bool LooksLikeNamedDeclaration() const noexcept;
  bool ParseSummary(ViewDefinition &view) noexcept;
  bool ParseTemplateString(const Token &token, SummarySpec &summary) noexcept;
  std::optional<std::string> ParseStringLiteral(const Token &token) noexcept;

  [[nodiscard]] bool LooksLikeType(u32 ahead) const noexcept;
  [[nodiscard]] bool LooksLikeCast() const noexcept;
  bool ParseType(TypeExpr &type) noexcept;
  u32 AddType(TypeExpr type) noexcept;

  ExprId ParseExpression() noexcept;
  ExprId ParseTernary() noexcept;
  ExprId ParseBinary(int minPrecedence) noexcept;
  ExprId ParseUnary() noexcept;
  ExprId ParsePostfix(ExprId base) noexcept;
  ExprId ParsePrimary() noexcept;
  ExprId ParseCall(const Token &name) noexcept;
};

void
Parser::ParseFile() noexcept
{
  while (!Peek().Is(TokenKind::Eof)) {
    if (Peek().Is(TokenKind::Error)) {
      Error(Peek(), "");
      mFile.mDiagnostics.push_back(*TakeError());
      break;
    }
    ParseView();
  }
}

void
Parser::SkipView(u32 viewStart) noexcept
{
  // Recover by skipping to the closing brace of this view's body, or to the next `view` at the top level.
  mPos = viewStart;
  Advance();
  int depth = 0;
  while (!Peek().Is(TokenKind::Eof)) {
    if (Peek().Is(TokenKind::Error)) {
      // An error token can't be skipped meaningfully (e.g. an unterminated comment swallowed the rest).
      while (!Peek().Is(TokenKind::Eof)) {
        Advance();
      }
      return;
    }
    if (depth == 0 && Peek().IsIdentifier("view")) {
      return;
    }
    if (Peek().Is(TokenKind::LBrace)) {
      ++depth;
    } else if (Peek().Is(TokenKind::RBrace)) {
      if (--depth <= 0) {
        Advance();
        return;
      }
    }
    Advance();
  }
}

void
Parser::ParseView() noexcept
{
  const u32 viewStart = mPos;
  const Token &start = Peek();
  if (!AcceptIdentifier("view")) {
    Error(start, std::format("expected 'view', found '{}'", start.mText));
    mFile.mDiagnostics.push_back(*TakeError());
    SkipView(viewStart);
    return;
  }

  ViewDefinition view;
  view.mLoc = start.mLoc;
  view.mFile = &mFile;

  mTypeNames.clear();
  bool ok = ParsePattern(view.mPattern);
  if (ok && AcceptIdentifier("in")) {
    const Token &objfile = Peek();
    if (!objfile.Is(TokenKind::String)) {
      Error(objfile, "expected object file name string after 'in'");
      ok = false;
    } else {
      Advance();
      auto name = ParseStringLiteral(objfile);
      ok = name.has_value();
      view.mObjectFile = std::move(name);
    }
  }

  ok = ok && ParseViewBody(view);
  mTypeNames.clear();

  if (!ok) {
    mFile.mDiagnostics.push_back(*TakeError());
    SkipView(viewStart);
    return;
  }
  mFile.mViews.push_back(std::move(view));
}

bool
Parser::ParsePattern(TypePattern &pattern) noexcept
{
  const Token &token = Peek();
  pattern.mLoc = token.mLoc;
  if (token.Is(TokenKind::Pattern)) {
    pattern.mSource = std::string{ token.mText };
  } else if (token.Is(TokenKind::String)) {
    // Taken as is: backslashes are regex escapes here, not string escapes.
    pattern.mSource = std::string{ token.mText.substr(1, token.mText.size() - 2) };
  } else {
    Error(token, std::format("expected a type pattern after 'view', found '{}'", token.mText));
    return false;
  }
  Advance();

  if (pattern.mSource.empty() || pattern.mSource == "{") {
    Error(token, "expected a type pattern after 'view'");
    return false;
  }
  if (pattern.mSource.ends_with('{')) {
    // The pattern ends at whitespace, so `view Foo{` swallowed the brace.
    Error(token,
      std::format(
        "put a space between the pattern '{}' and '{{'", pattern.mSource.substr(0, pattern.mSource.size() - 1)));
    return false;
  }

  try {
    pattern.mRegex = std::regex{ pattern.mSource, std::regex::ECMAScript | std::regex::optimize };
  } catch (const std::regex_error &error) {
    Error(token, std::format("invalid type pattern '{}': {}", pattern.mSource, error.what()));
    return false;
  }
  return true;
}

bool
Parser::IsDeclared(const ViewDefinition &view, std::string_view name) const noexcept
{
  return std::ranges::any_of(view.mLets, [name](const LetBinding &let) { return let.mName == name; }) ||
         std::ranges::any_of(view.mUsings, [name](const UsingBinding &decl) { return decl.mName == name; }) ||
         std::ranges::any_of(view.mFunctions, [name](const FunctionDef &fn) { return fn.mName == name; });
}

bool
Parser::ParseUsing(ViewDefinition &view) noexcept
{
  // `using` already consumed
  if (!Peek().Is(TokenKind::Identifier)) {
    Error(Peek(), "expected a name after 'using'");
    return false;
  }
  const Token &name = Advance();
  if (IsDeclared(view, name.mText)) {
    Error(name, std::format("'{}' is already defined", name.mText));
    return false;
  }
  if (!Expect(TokenKind::Equal, "after the using name")) {
    return false;
  }

  UsingBinding binding{ .mName = name.mText, .mLoc = name.mLoc };
  if (Peek().IsIdentifier("typeof") && Peek(1).Is(TokenKind::LParen)) {
    Advance();
    Advance();
    binding.mKind = UsingBinding::Kind::TypeOf;
    binding.mExpr = ParseExpression();
    if (Failed() || !Expect(TokenKind::RParen, "to close typeof(")) {
      return false;
    }
  } else if (Peek().IsIdentifier("template_arg") && Peek(1).Is(TokenKind::LParen)) {
    Advance();
    Advance();
    if (!Peek().Is(TokenKind::Integer)) {
      Error(Peek(), "template_arg() takes the index of a template argument, e.g. template_arg(0)");
      return false;
    }
    binding.mKind = UsingBinding::Kind::TemplateArgument;
    binding.mIndex = static_cast<u32>(Advance().mInteger);
    if (!Expect(TokenKind::RParen, "to close template_arg(")) {
      return false;
    }
  } else {
    TypeExpr type;
    if (!ParseType(type)) {
      return false;
    }
    binding.mKind = UsingBinding::Kind::Type;
    binding.mType = AddType(std::move(type));
  }

  view.mDeclarations.push_back(
    Declaration{ .mKind = Declaration::Kind::Using, .mIndex = static_cast<u32>(view.mUsings.size()) });
  view.mUsings.push_back(binding);
  mTypeNames.push_back(name.mText);
  return true;
}

bool
Parser::ParseViewBody(ViewDefinition &view) noexcept
{
  if (!Expect(TokenKind::LBrace, "to start the view body")) {
    return false;
  }

  bool seenSummary = false;
  bool seenChildren = false;
  while (!Accept(TokenKind::RBrace)) {
    const Token &item = Peek();
    if (item.IsIdentifier("let")) {
      Advance();
      if (!Peek().Is(TokenKind::Identifier)) {
        Error(Peek(), "expected a name after 'let'");
        return false;
      }
      const Token &name = Advance();
      if (IsDeclared(view, name.mText)) {
        Error(name, std::format("'{}' is already defined", name.mText));
        return false;
      }
      if (!Expect(TokenKind::Equal, "after the let name")) {
        return false;
      }
      const ExprId expr = ParseExpression();
      if (Failed()) {
        return false;
      }
      view.mDeclarations.push_back(
        Declaration{ .mKind = Declaration::Kind::Let, .mIndex = static_cast<u32>(view.mLets.size()) });
      view.mLets.push_back(LetBinding{ .mName = name.mText, .mExpr = expr, .mLoc = name.mLoc });
    } else if (item.IsIdentifier("using")) {
      Advance();
      if (!ParseUsing(view)) {
        return false;
      }
    } else if (item.IsIdentifier("fn")) {
      Advance();
      if (!Peek().Is(TokenKind::Identifier)) {
        Error(Peek(), "expected a function name");
        return false;
      }
      const Token &name = Advance();
      if (std::ranges::any_of(
            view.mFunctions, [&name](const FunctionDef &fn) { return fn.mName == name.mText; })) {
        Error(name, std::format("function '{}' is already defined", name.mText));
        return false;
      }
      if (IsDeclared(view, name.mText)) {
        Error(name, std::format("'{}' is already defined", name.mText));
        return false;
      }
      FunctionDef function{ .mName = name.mText, .mLoc = name.mLoc };
      if (!Expect(TokenKind::LParen, "after the function name")) {
        return false;
      }
      if (!Peek().Is(TokenKind::RParen)) {
        while (true) {
          TypeExpr type;
          if (!ParseType(type)) {
            return false;
          }
          const u32 typeId = AddType(std::move(type));
          const bool isReference = Accept(TokenKind::Amp);
          if (!Peek().Is(TokenKind::Identifier)) {
            Error(Peek(), "expected a parameter name");
            return false;
          }
          const Token &paramName = Advance();
          function.mParams.push_back(Parameter{
            .mName = paramName.mText, .mType = typeId, .mIsReference = isReference, .mLoc = paramName.mLoc });
          if (!Accept(TokenKind::Comma)) {
            break;
          }
        }
      }
      if (!Expect(TokenKind::RParen, "after the function parameters")) {
        return false;
      }
      if (Accept(TokenKind::Arrow)) {
        TypeExpr type;
        if (!ParseType(type)) {
          return false;
        }
        function.mReturnType = AddType(std::move(type));
      }
      if (!Peek().Is(TokenKind::LBrace)) {
        Expect(TokenKind::LBrace, "to start the function body");
        return false;
      }
      function.mBody = ParseBlock();
      if (Failed()) {
        return false;
      }
      view.mFunctions.push_back(std::move(function));
    } else if (item.IsIdentifier("summary")) {
      if (seenSummary) {
        Error(item, "a view can only have one summary");
        return false;
      }
      seenSummary = true;
      Advance();
      if (!ParseSummary(view)) {
        return false;
      }
    } else if (item.IsIdentifier("children")) {
      if (seenChildren) {
        Error(item, "a view can only have one children specification");
        return false;
      }
      seenChildren = true;
      Advance();
      view.mChildren.mLoc = Peek().mLoc;
      if (AcceptIdentifier("none")) {
        view.mChildren.mKind = ChildrenSpec::Kind::None;
      } else if (AcceptIdentifier("default")) {
        view.mChildren.mKind = ChildrenSpec::Kind::Default;
      } else if (Peek().Is(TokenKind::LBrace)) {
        view.mChildren.mKind = ChildrenSpec::Kind::Block;
        view.mChildren.mBody = ParseBlock();
        if (Failed()) {
          return false;
        }
      } else if (Peek().Is(TokenKind::LParen) && Peek(1).Is(TokenKind::Identifier) &&
                 Peek(2).Is(TokenKind::Colon)) {
        view.mChildren.mKind = ChildrenSpec::Kind::Block;
        bool seenSize = false;
        bool seenLimit = false;
        Advance();
        while (true) {
          const Token &option = Peek();
          if (!option.Is(TokenKind::Identifier)) {
            Error(option, "expected a children option");
            return false;
          }
          Advance();
          if (!Expect(TokenKind::Colon, "after the children option")) {
            return false;
          }
          if (option.mText == "size") {
            if (seenSize) {
              Error(option, "repeated children option 'size'");
              return false;
            }
            seenSize = true;
            view.mChildren.mSize = ParseExpression();
            if (Failed()) {
              return false;
            }
          } else if (option.mText == "limit") {
            if (seenLimit) {
              Error(option, "repeated children option 'limit'");
              return false;
            }
            seenLimit = true;
            if (!Peek().Is(TokenKind::Integer)) {
              Error(Peek(), "children limit must be an integer literal");
              return false;
            }
            view.mChildren.mLimit = Advance().mInteger;
          } else {
            Error(option, std::format("unknown children option '{}'", option.mText));
            return false;
          }
          if (!Accept(TokenKind::Comma)) {
            break;
          }
        }
        if (!Expect(TokenKind::RParen, "after the children options")) {
          return false;
        }
        if (Peek().Is(TokenKind::LBrace)) {
          view.mChildren.mBody = ParseBlock();
        } else if (Peek().Is(TokenKind::Identifier) && Peek(1).Is(TokenKind::LParen)) {
          const ExprId call = ParseExpression();
          if (Failed()) {
            return false;
          }
          if (mFile.mArena.Get(call).mKind != ExprKind::Call) {
            Error(Peek(), "children options must be followed by a block or a call");
            return false;
          }
          const StmtId callStmt =
            AddStatement(Stmt{ .mKind = StmtKind::Call, .mLoc = mFile.mArena.Get(call).mLoc, .mExpr = call });
          Stmt block{ .mKind = StmtKind::Block,
            .mLoc = view.mChildren.mLoc,
            .mFirst = static_cast<u32>(mFile.mArena.mBlockItems.size()),
            .mCount = 1 };
          mFile.mArena.mBlockItems.push_back(callStmt);
          view.mChildren.mBody = AddStatement(std::move(block));
          view.mChildren.mIsCallForm = true;
        } else {
          Error(Peek(), "children options must be followed by a block or a call");
          return false;
        }
        if (Failed()) {
          return false;
        }
      } else {
        view.mChildren.mKind = ChildrenSpec::Kind::Expression;
        view.mChildren.mExpr = ParseExpression();
        if (Failed()) {
          return false;
        }
      }
    } else {
      Error(item, std::format("expected 'let', 'using', 'summary', 'children' or '}}', found '{}'", item.mText));
      return false;
    }
    // Items may optionally be terminated by a semicolon.
    Accept(TokenKind::Semicolon);
  }
  return true;
}

bool
Parser::LooksLikeNamedDeclaration() const noexcept
{
  if (!Peek().Is(TokenKind::Identifier) || IsBuiltinTypeKeyword(Peek().mText)) {
    return false;
  }
  u32 i = 1;
  while (Peek(i).Is(TokenKind::ColonColon) && Peek(i + 1).Is(TokenKind::Identifier)) {
    i += 2;
  }
  while (Peek(i).Is(TokenKind::Star)) {
    ++i;
  }
  return Peek(i).Is(TokenKind::Identifier) && Peek(i + 1).Is(TokenKind::Equal);
}

StmtId
Parser::ParseBlock() noexcept
{
  const Token &open = Peek();
  if (!Expect(TokenKind::LBrace, "to start a block")) {
    return kNoStmt;
  }
  std::vector<StmtId> items;
  while (!Peek().Is(TokenKind::RBrace) && !Peek().Is(TokenKind::Eof) && !Failed()) {
    const StmtId item = ParseStatement();
    if (Failed()) {
      return kNoStmt;
    }
    items.push_back(item);
  }
  if (!Expect(TokenKind::RBrace, "to end a block")) {
    return kNoStmt;
  }
  Stmt block{ .mKind = StmtKind::Block,
    .mLoc = open.mLoc,
    .mFirst = static_cast<u32>(mFile.mArena.mBlockItems.size()),
    .mCount = static_cast<u32>(items.size()) };
  mFile.mArena.mBlockItems.insert(mFile.mArena.mBlockItems.end(), items.begin(), items.end());
  return AddStatement(std::move(block));
}

StmtId
Parser::ParseDeclaration() noexcept
{
  const Token &start = Peek();
  Stmt stmt{ .mKind = StmtKind::Declare, .mLoc = start.mLoc };
  if (AcceptIdentifier("auto")) {
    if (!Peek().Is(TokenKind::Identifier)) {
      Error(Peek(), "expected a name after 'auto'");
      return kNoStmt;
    }
    stmt.mName = Advance().mText;
  } else {
    TypeExpr type;
    if (!ParseType(type)) {
      return kNoStmt;
    }
    stmt.mType = AddType(std::move(type));
    if (!Peek().Is(TokenKind::Identifier)) {
      Error(Peek(), "expected a name after the type");
      return kNoStmt;
    }
    stmt.mName = Advance().mText;
  }
  if (!Expect(TokenKind::Equal, "in a declaration")) {
    return kNoStmt;
  }
  stmt.mExpr = ParseExpression();
  if (Failed()) {
    return kNoStmt;
  }
  return AddStatement(std::move(stmt));
}

StmtId
Parser::ParseAssignmentOrIncrement() noexcept
{
  const Token &start = Peek();
  Stmt stmt{ .mKind = StmtKind::Assign, .mLoc = start.mLoc };
  if (Accept(TokenKind::PlusPlus) || Accept(TokenKind::MinusMinus)) {
    stmt.mKind = StmtKind::Increment;
    stmt.mOperator = start.Is(TokenKind::PlusPlus) ? AssignOp::Add : AssignOp::Sub;
    if (!Peek().Is(TokenKind::Identifier)) {
      Error(Peek(), "expected a name after increment operator");
      return kNoStmt;
    }
    stmt.mName = Advance().mText;
  } else {
    if (!Peek().Is(TokenKind::Identifier)) {
      Error(Peek(), "expected a name in assignment");
      return kNoStmt;
    }
    stmt.mName = Advance().mText;
    if (Accept(TokenKind::Equal)) {
      stmt.mOperator = AssignOp::Set;
    } else if (Accept(TokenKind::PlusEqual)) {
      stmt.mOperator = AssignOp::Add;
    } else if (Accept(TokenKind::MinusEqual)) {
      stmt.mOperator = AssignOp::Sub;
    } else if (Accept(TokenKind::PlusPlus)) {
      stmt.mKind = StmtKind::Increment;
      stmt.mOperator = AssignOp::Add;
    } else if (Accept(TokenKind::MinusMinus)) {
      stmt.mKind = StmtKind::Increment;
      stmt.mOperator = AssignOp::Sub;
    } else {
      Error(Peek(), "expected an assignment or increment operator");
      return kNoStmt;
    }
  }
  if (stmt.mKind != StmtKind::Increment) {
    stmt.mExpr = ParseExpression();
    if (Failed()) {
      return kNoStmt;
    }
  }
  return AddStatement(std::move(stmt));
}

StmtId
Parser::ParseSimpleStatement() noexcept
{
  if (Peek().IsIdentifier("auto") || LooksLikeType(0) || LooksLikeNamedDeclaration()) {
    return ParseDeclaration();
  }
  return ParseAssignmentOrIncrement();
}

StmtId
Parser::ParseStatement() noexcept
{
  const Token &start = Peek();
  if (start.Is(TokenKind::LBrace)) {
    return ParseBlock();
  }
  if (AcceptIdentifier("if")) {
    Stmt stmt{ .mKind = StmtKind::If, .mLoc = start.mLoc };
    if (!Expect(TokenKind::LParen, "after 'if'")) {
      return kNoStmt;
    }
    stmt.mExpr = ParseExpression();
    if (Failed() || !Expect(TokenKind::RParen, "after the condition")) {
      return kNoStmt;
    }
    stmt.mA = ParseStatement();
    if (Failed()) {
      return kNoStmt;
    }
    if (AcceptIdentifier("else")) {
      stmt.mB = ParseStatement();
      if (Failed()) {
        return kNoStmt;
      }
    }
    return AddStatement(std::move(stmt));
  }
  if (AcceptIdentifier("while")) {
    Stmt stmt{ .mKind = StmtKind::While, .mLoc = start.mLoc };
    if (!Expect(TokenKind::LParen, "after 'while'")) {
      return kNoStmt;
    }
    stmt.mExpr = ParseExpression();
    if (Failed() || !Expect(TokenKind::RParen, "after the condition")) {
      return kNoStmt;
    }
    stmt.mA = ParseStatement();
    return Failed() ? kNoStmt : AddStatement(std::move(stmt));
  }
  if (AcceptIdentifier("for")) {
    Stmt stmt{ .mKind = StmtKind::For, .mLoc = start.mLoc };
    if (!Expect(TokenKind::LParen, "after 'for'")) {
      return kNoStmt;
    }
    if (!Peek().Is(TokenKind::Semicolon)) {
      stmt.mA = ParseSimpleStatement();
      if (Failed()) {
        return kNoStmt;
      }
    }
    if (!Expect(TokenKind::Semicolon, "after the for initializer")) {
      return kNoStmt;
    }
    if (!Peek().Is(TokenKind::Semicolon)) {
      stmt.mExpr = ParseExpression();
      if (Failed()) {
        return kNoStmt;
      }
    }
    if (!Expect(TokenKind::Semicolon, "after the for condition")) {
      return kNoStmt;
    }
    if (!Peek().Is(TokenKind::RParen)) {
      stmt.mB = ParseSimpleStatement();
      if (Failed()) {
        return kNoStmt;
      }
    }
    if (!Expect(TokenKind::RParen, "after the for step")) {
      return kNoStmt;
    }
    stmt.mC = ParseStatement();
    return Failed() ? kNoStmt : AddStatement(std::move(stmt));
  }
  if (AcceptIdentifier("continue") || AcceptIdentifier("break")) {
    const bool isContinue = start.IsIdentifier("continue");
    if (!Expect(TokenKind::Semicolon, "after the loop control statement")) {
      return kNoStmt;
    }
    return AddStatement(Stmt{ .mKind = isContinue ? StmtKind::Continue : StmtKind::Break, .mLoc = start.mLoc });
  }
  if (AcceptIdentifier("return")) {
    Stmt stmt{ .mKind = StmtKind::Return, .mLoc = start.mLoc };
    if (!Peek().Is(TokenKind::Semicolon)) {
      stmt.mExpr = ParseExpression();
      if (Failed()) {
        return kNoStmt;
      }
    }
    if (!Expect(TokenKind::Semicolon, "after return")) {
      return kNoStmt;
    }
    return AddStatement(std::move(stmt));
  }
  if (AcceptIdentifier("yield")) {
    Stmt stmt{ .mKind = StmtKind::Yield, .mLoc = start.mLoc };
    if (Accept(TokenKind::LBracket)) {
      stmt.mKey = ParseExpression();
      if (Failed() || !Expect(TokenKind::RBracket, "after the yield key")) {
        return kNoStmt;
      }
    }
    stmt.mExpr = ParseExpression();
    if (Failed() || !Expect(TokenKind::Semicolon, "after yield")) {
      return kNoStmt;
    }
    return AddStatement(std::move(stmt));
  }
  if (Peek().IsIdentifier("auto") || LooksLikeType(0) || LooksLikeNamedDeclaration()) {
    const StmtId stmt = ParseDeclaration();
    if (Failed() || !Expect(TokenKind::Semicolon, "after the declaration")) {
      return kNoStmt;
    }
    return stmt;
  }
  if (start.Is(TokenKind::PlusPlus) || start.Is(TokenKind::MinusMinus) ||
      (start.Is(TokenKind::Identifier) &&
        (Peek(1).Is(TokenKind::Equal) || Peek(1).Is(TokenKind::PlusEqual) || Peek(1).Is(TokenKind::MinusEqual) ||
          Peek(1).Is(TokenKind::PlusPlus) || Peek(1).Is(TokenKind::MinusMinus)))) {
    const StmtId stmt = ParseAssignmentOrIncrement();
    if (Failed() || !Expect(TokenKind::Semicolon, "after the assignment")) {
      return kNoStmt;
    }
    return stmt;
  }
  if (start.Is(TokenKind::Identifier) && Peek(1).Is(TokenKind::LParen)) {
    const ExprId call = ParseExpression();
    if (Failed()) {
      return kNoStmt;
    }
    if (mFile.mArena.Get(call).mKind != ExprKind::Call) {
      Error(start, "expected a call");
      return kNoStmt;
    }
    if (!Expect(TokenKind::Semicolon, "after the call")) {
      return kNoStmt;
    }
    return AddStatement(Stmt{ .mKind = StmtKind::Call, .mLoc = start.mLoc, .mExpr = call });
  }
  Error(start, "expected a statement");
  return kNoStmt;
}

bool
Parser::ParseSummary(ViewDefinition &view) noexcept
{
  SummarySpec &summary = view.mSummary;
  summary.mLoc = Peek().mLoc;
  if (Peek().Is(TokenKind::String)) {
    summary.mKind = SummarySpec::Kind::Template;
    return ParseTemplateString(Advance(), summary);
  }
  summary.mKind = SummarySpec::Kind::Expression;
  summary.mExpr = ParseExpression();
  if (Failed()) {
    return false;
  }
  if (Accept(TokenKind::Comma)) {
    // summary expr, x
    const Token &spec = Peek();
    if (!spec.Is(TokenKind::Identifier) || spec.mText.size() != 1 ||
        std::string_view{ "xXdobc" }.find(spec.mText[0]) == std::string_view::npos) {
      Error(spec, "expected a format specifier (x, X, d, o, b or c)");
      return false;
    }
    summary.mFormat = static_cast<FormatSpec>(Advance().mText[0]);
  }
  return true;
}

std::optional<std::string>
Parser::ParseStringLiteral(const Token &token) noexcept
{
  std::string result;
  const auto body = token.mText.substr(1, token.mText.size() - 2);
  if (auto err = Unescape(body, result); err) {
    Error(token, *err);
    return std::nullopt;
  }
  return result;
}

bool
Parser::ParseTemplateString(const Token &token, SummarySpec &summary) noexcept
{
  // The raw body, between the quotes. Literal text is unescaped, `{...}` contains an expression which is parsed
  // with its own lexer (escapes are not processed inside it).
  const std::string_view body = token.mText.substr(1, token.mText.size() - 2);
  std::string literal;
  size_t literalStart = 0;

  const auto flushLiteral = [&](size_t end) -> bool {
    if (auto err = Unescape(body.substr(literalStart, end - literalStart), literal); err) {
      Error(token, *err);
      return false;
    }
    return true;
  };

  size_t i = 0;
  while (i < body.size()) {
    const char c = body[i];
    if (c == '\\') {
      i += 2;
      continue;
    }
    if (c == '}') {
      if (i + 1 < body.size() && body[i + 1] == '}') {
        if (!flushLiteral(i)) {
          return false;
        }
        literal.push_back('}');
        i += 2;
        literalStart = i;
        continue;
      }
      Token at = token;
      at.mLoc.mColumn += static_cast<u32>(i + 1);
      Error(at, "unmatched '}' in summary template (write '}}' for a literal brace)");
      return false;
    }
    if (c != '{') {
      ++i;
      continue;
    }
    if (i + 1 < body.size() && body[i + 1] == '{') {
      if (!flushLiteral(i)) {
        return false;
      }
      literal.push_back('{');
      i += 2;
      literalStart = i;
      continue;
    }

    if (!flushLiteral(i)) {
      return false;
    }
    // Find the matching close brace
    size_t depth = 1;
    size_t j = i + 1;
    for (; j < body.size(); ++j) {
      if (body[j] == '{') {
        ++depth;
      } else if (body[j] == '}' && --depth == 0) {
        break;
      }
    }
    Token at = token;
    at.mLoc.mColumn += static_cast<u32>(i + 1);
    if (j >= body.size()) {
      Error(at, "unterminated '{' in summary template");
      return false;
    }

    const std::string_view exprSource = body.substr(i + 1, j - i - 1);
    SourceLoc exprLoc = token.mLoc;
    exprLoc.mColumn += static_cast<u32>(i + 2);
    Lexer lexer{ exprSource, exprLoc, 1 };
    std::vector<Token> tokens;
    do {
      tokens.push_back(lexer.Next());
    } while (!tokens.back().Is(TokenKind::Eof) && !tokens.back().Is(TokenKind::Error));
    if (tokens.back().Is(TokenKind::Error)) {
      Error(tokens.back(), "");
      return false;
    }
    if (tokens.size() == 1) {
      Error(at, "empty '{}' in summary template");
      return false;
    }

    Parser sub{ mFile, std::move(tokens) };
    FormatSpec format = FormatSpec::Default;
    const ExprId expr = sub.ParseStandaloneExpression(format, mTypeNames);
    if (auto err = sub.TakeError(); err) {
      mError = std::move(err);
      return false;
    }
    summary.mParts.push_back(TemplatePart{ .mLiteral = std::move(literal), .mExpr = expr, .mFormat = format });
    literal.clear();
    i = j + 1;
    literalStart = i;
  }

  if (!flushLiteral(body.size())) {
    return false;
  }
  if (!literal.empty() || summary.mParts.empty()) {
    summary.mParts.push_back(TemplatePart{ .mLiteral = std::move(literal) });
  }
  return true;
}

ExprId
Parser::ParseStandaloneExpression(FormatSpec &format, std::span<const std::string_view> typeNames) noexcept
{
  mTypeNames.assign(typeNames.begin(), typeNames.end());
  const ExprId expr = ParseExpression();
  if (Failed()) {
    return kNoExpr;
  }
  if (Accept(TokenKind::Comma)) {
    const Token &spec = Peek();
    if (!spec.Is(TokenKind::Identifier) || spec.mText.size() != 1 ||
        std::string_view{ "xXdobc" }.find(spec.mText[0]) == std::string_view::npos) {
      Error(spec, "expected a format specifier (x, X, d, o, b or c)");
      return kNoExpr;
    }
    format = static_cast<FormatSpec>(Advance().mText[0]);
  }
  if (!Peek().Is(TokenKind::Eof)) {
    Error(Peek(), std::format("unexpected '{}' in summary expression", Peek().mText));
    return kNoExpr;
  }
  return expr;
}

u32
Parser::AddType(TypeExpr type) noexcept
{
  mFile.mArena.mTypes.push_back(std::move(type));
  return static_cast<u32>(mFile.mArena.mTypes.size() - 1);
}

bool
Parser::LooksLikeType(u32 ahead) const noexcept
{
  const Token &first = Peek(ahead);
  if (first.Is(TokenKind::ColonColon)) {
    return true;
  }
  if (!first.Is(TokenKind::Identifier)) {
    return false;
  }
  if (IsBuiltinTypeKeyword(first.mText)) {
    return true;
  }
  if (IsTypeName(first.mText)) {
    return true;
  }
  // `ns::Foo`: expressions have no scope resolution, so this must be a type.
  if (Peek(ahead + 1).Is(TokenKind::ColonColon)) {
    return true;
  }
  // `Foo*)`, `Foo**)`: as an expression this could never be complete.
  u32 i = ahead + 1;
  if (!Peek(i).Is(TokenKind::Star)) {
    return false;
  }
  while (Peek(i).Is(TokenKind::Star)) {
    ++i;
  }
  return Peek(i).Is(TokenKind::RParen);
}

bool
Parser::LooksLikeCast() const noexcept
{
  if (!Peek().Is(TokenKind::LParen) || !LooksLikeType(1)) {
    return false;
  }
  return true;
}

bool
Parser::ParseType(TypeExpr &type) noexcept
{
  type.mLoc = Peek().mLoc;
  while (AcceptIdentifier("const") || AcceptIdentifier("volatile")) {
  }

  if (Peek().Is(TokenKind::Identifier) && IsBuiltinTypeKeyword(Peek().mText)) {
    type.mIsBuiltin = true;
    while (Peek().Is(TokenKind::Identifier) && IsBuiltinTypeKeyword(Peek().mText)) {
      const Token &word = Advance();
      if (word.mText != "const" && word.mText != "volatile") {
        type.mNameParts.push_back(word.mText);
      }
    }
    if (type.mNameParts.empty()) {
      Error(Peek(), "expected a type");
      return false;
    }
  } else {
    type.mIsRooted = Accept(TokenKind::ColonColon);
    if (!Peek().Is(TokenKind::Identifier)) {
      Error(Peek(), std::format("expected a type, found '{}'", Peek().mText));
      return false;
    }
    type.mNameParts.push_back(Advance().mText);
    while (Accept(TokenKind::ColonColon)) {
      if (!Peek().Is(TokenKind::Identifier)) {
        Error(Peek(), "expected identifier after '::'");
        return false;
      }
      type.mNameParts.push_back(Advance().mText);
    }
    if (Peek().Is(TokenKind::Less)) {
      Error(Peek(),
        "template types can't be written out yet; name them with 'using T = template_arg(N)' or "
        "'using T = typeof(expression)' instead");
      return false;
    }
    AcceptIdentifier("const");
  }

  while (Accept(TokenKind::Star)) {
    ++type.mPointerDepth;
    while (AcceptIdentifier("const") || AcceptIdentifier("volatile")) {
    }
  }
  return true;
}

ExprId
Parser::ParseExpression() noexcept
{
  return ParseTernary();
}

ExprId
Parser::ParseTernary() noexcept
{
  const ExprId condition = ParseBinary(0);
  if (Failed() || !Peek().Is(TokenKind::Question)) {
    return condition;
  }
  const SourceLoc loc = Advance().mLoc;
  const ExprId whenTrue = ParseExpression();
  if (Failed() || !Expect(TokenKind::Colon, "in conditional expression")) {
    return kNoExpr;
  }
  const ExprId whenFalse = ParseTernary();
  if (Failed()) {
    return kNoExpr;
  }
  const ExprId id = AddNewExpression(ExprKind::Ternary, loc);
  Get(id).mA = condition;
  Get(id).mB = whenTrue;
  Get(id).mC = whenFalse;
  return id;
}

struct BinaryOperatorInfo
{
  TokenKind mToken;
  BinaryOp mOp;
  int mPrecedence;
};

static constexpr auto kBinaryOperators = std::to_array<BinaryOperatorInfo>({
  { TokenKind::PipePipe, BinaryOp::LogicalOr, 1 },
  { TokenKind::AmpAmp, BinaryOp::LogicalAnd, 2 },
  { TokenKind::Pipe, BinaryOp::BitOr, 3 },
  { TokenKind::Caret, BinaryOp::BitXor, 4 },
  { TokenKind::Amp, BinaryOp::BitAnd, 5 },
  { TokenKind::EqualEqual, BinaryOp::Equal, 6 },
  { TokenKind::BangEqual, BinaryOp::NotEqual, 6 },
  { TokenKind::Less, BinaryOp::Less, 7 },
  { TokenKind::LessEqual, BinaryOp::LessEqual, 7 },
  { TokenKind::Greater, BinaryOp::Greater, 7 },
  { TokenKind::GreaterEqual, BinaryOp::GreaterEqual, 7 },
  { TokenKind::LessLess, BinaryOp::Shl, 8 },
  { TokenKind::GreaterGreater, BinaryOp::Shr, 8 },
  { TokenKind::Plus, BinaryOp::Add, 9 },
  { TokenKind::Minus, BinaryOp::Sub, 9 },
  { TokenKind::Star, BinaryOp::Mul, 10 },
  { TokenKind::Slash, BinaryOp::Div, 10 },
  { TokenKind::Percent, BinaryOp::Mod, 10 },
});

ExprId
Parser::ParseBinary(int minPrecedence) noexcept
{
  ExprId lhs = ParseUnary();
  while (!Failed()) {
    const Token &token = Peek();
    const auto *info = std::ranges::find_if(
      kBinaryOperators, [&token](const BinaryOperatorInfo &op) { return op.mToken == token.mKind; });
    if (info == kBinaryOperators.end() || info->mPrecedence <= minPrecedence) {
      break;
    }
    Advance();
    const ExprId rhs = ParseBinary(info->mPrecedence);
    if (Failed()) {
      return kNoExpr;
    }
    const ExprId id = AddNewExpression(ExprKind::Binary, token.mLoc);
    Get(id).mOp = static_cast<u8>(info->mOp);
    Get(id).mA = lhs;
    Get(id).mB = rhs;
    lhs = id;
  }
  return Failed() ? kNoExpr : lhs;
}

ExprId
Parser::ParseUnary() noexcept
{
  const Token &token = Peek();
  std::optional<UnaryOp> op;
  switch (token.mKind) {
  case TokenKind::Plus:
    op = UnaryOp::Plus;
    break;
  case TokenKind::Minus:
    op = UnaryOp::Negate;
    break;
  case TokenKind::Bang:
    op = UnaryOp::LogicalNot;
    break;
  case TokenKind::Tilde:
    op = UnaryOp::BitNot;
    break;
  case TokenKind::Star:
    op = UnaryOp::Deref;
    break;
  case TokenKind::Amp:
    op = UnaryOp::AddressOf;
    break;
  default:
    break;
  }

  if (op) {
    Advance();
    const ExprId operand = ParseUnary();
    if (Failed()) {
      return kNoExpr;
    }
    const ExprId id = AddNewExpression(ExprKind::Unary, token.mLoc);
    Get(id).mOp = static_cast<u8>(*op);
    Get(id).mA = operand;
    return id;
  }

  if (LooksLikeCast()) {
    Advance();
    TypeExpr type;
    if (!ParseType(type) || !Expect(TokenKind::RParen, "after the type in a cast")) {
      return kNoExpr;
    }
    const ExprId operand = ParseUnary();
    if (Failed()) {
      return kNoExpr;
    }
    const ExprId id = AddNewExpression(ExprKind::Cast, token.mLoc);
    Get(id).mType = AddType(std::move(type));
    Get(id).mA = operand;
    return id;
  }

  const ExprId primary = ParsePrimary();
  if (Failed()) {
    return kNoExpr;
  }
  return ParsePostfix(primary);
}

ExprId
Parser::ParsePostfix(ExprId base) noexcept
{
  while (!Failed()) {
    const Token &token = Peek();
    if (token.Is(TokenKind::Dot) || token.Is(TokenKind::Arrow)) {
      Advance();
      if (!Peek().Is(TokenKind::Identifier)) {
        Error(Peek(), std::format("expected a member name after '{}'", token.mText));
        return kNoExpr;
      }
      const Token &member = Advance();
      const ExprId id =
        AddNewExpression(token.Is(TokenKind::Dot) ? ExprKind::Member : ExprKind::Arrow, member.mLoc);
      Get(id).mA = base;
      Get(id).mName = member.mText;
      base = id;
    } else if (token.Is(TokenKind::LBracket)) {
      Advance();
      const ExprId index = ParseExpression();
      if (Failed() || !Expect(TokenKind::RBracket, "after the index")) {
        return kNoExpr;
      }
      const ExprId id = AddNewExpression(ExprKind::Index, token.mLoc);
      Get(id).mA = base;
      Get(id).mB = index;
      base = id;
    } else {
      break;
    }
  }
  return Failed() ? kNoExpr : base;
}

ExprId
Parser::ParseCall(const Token &name) noexcept
{
  // `(` already consumed
  const ExprId id = AddNewExpression(ExprKind::Call, name.mLoc);
  std::vector<CallArg> args;
  bool seenNamed = false;
  if (!Accept(TokenKind::RParen)) {
    while (true) {
      CallArg arg;
      if (Peek().Is(TokenKind::Identifier) && Peek(1).Is(TokenKind::Colon)) {
        arg.mName = Advance().mText;
        Advance();
        seenNamed = true;
      } else if (seenNamed) {
        Error(Peek(), "positional arguments must come before named arguments");
        return kNoExpr;
      }
      arg.mExpr = ParseExpression();
      if (Failed()) {
        return kNoExpr;
      }
      args.push_back(arg);
      if (Accept(TokenKind::RParen)) {
        break;
      }
      if (!Expect(TokenKind::Comma, "between call arguments")) {
        return kNoExpr;
      }
    }
  }
  auto &arena = mFile.mArena;
  Get(id).mName = name.mText;
  Get(id).mFirstArg = static_cast<u32>(arena.mArgs.size());
  Get(id).mArgCount = static_cast<u32>(args.size());
  arena.mArgs.insert(arena.mArgs.end(), args.begin(), args.end());
  return id;
}

ExprId
Parser::ParsePrimary() noexcept
{
  const Token &token = Peek();
  switch (token.mKind) {
  case TokenKind::Integer: {
    Advance();
    const ExprId id = AddNewExpression(ExprKind::IntLiteral, token.mLoc);
    Get(id).mInteger = token.mInteger;
    return id;
  }
  case TokenKind::Float: {
    Advance();
    const ExprId id = AddNewExpression(ExprKind::FloatLiteral, token.mLoc);
    Get(id).mFloat = token.mFloat;
    return id;
  }
  case TokenKind::Char: {
    Advance();
    std::string value;
    if (auto err = Unescape(token.mText.substr(1, token.mText.size() - 2), value); err) {
      Error(token, *err);
      return kNoExpr;
    }
    if (value.size() != 1) {
      Error(token, "character literals must contain exactly one (ASCII) character");
      return kNoExpr;
    }
    const ExprId id = AddNewExpression(ExprKind::CharLiteral, token.mLoc);
    Get(id).mInteger = static_cast<u8>(value[0]);
    return id;
  }
  case TokenKind::String: {
    Advance();
    auto value = ParseStringLiteral(token);
    if (!value) {
      return kNoExpr;
    }
    const ExprId id = AddNewExpression(ExprKind::StringLiteral, token.mLoc);
    mFile.mArena.mStrings.push_back(std::move(*value));
    Get(id).mString = static_cast<u32>(mFile.mArena.mStrings.size() - 1);
    return id;
  }
  case TokenKind::LParen: {
    Advance();
    const ExprId inner = ParseExpression();
    if (Failed() || !Expect(TokenKind::RParen, "to close the parenthesized expression")) {
      return kNoExpr;
    }
    return inner;
  }
  case TokenKind::Identifier: {
    Advance();
    if (token.mText == "this") {
      return AddNewExpression(ExprKind::This, token.mLoc);
    }
    if (token.mText == "true" || token.mText == "false") {
      const ExprId id = AddNewExpression(ExprKind::BoolLiteral, token.mLoc);
      Get(id).mInteger = token.mText == "true" ? 1 : 0;
      return id;
    }
    if (token.mText == "nullptr" || token.mText == "NULL") {
      return AddNewExpression(ExprKind::NullLiteral, token.mLoc);
    }
    if (token.mText == "sizeof") {
      if (!Expect(TokenKind::LParen, "after sizeof")) {
        return kNoExpr;
      }
      if (LooksLikeType(0)) {
        TypeExpr type;
        if (!ParseType(type) || !Expect(TokenKind::RParen, "after the type in sizeof")) {
          return kNoExpr;
        }
        const ExprId id = AddNewExpression(ExprKind::SizeofType, token.mLoc);
        Get(id).mType = AddType(std::move(type));
        return id;
      }
      const ExprId operand = ParseExpression();
      if (Failed() || !Expect(TokenKind::RParen, "after the sizeof expression")) {
        return kNoExpr;
      }
      const ExprId id = AddNewExpression(ExprKind::SizeofExpr, token.mLoc);
      Get(id).mA = operand;
      return id;
    }
    if (Accept(TokenKind::LParen)) {
      return ParseCall(token);
    }
    const ExprId id = AddNewExpression(ExprKind::Identifier, token.mLoc);
    Get(id).mName = token.mText;
    return id;
  }
  case TokenKind::Error:
    Error(token, "");
    return kNoExpr;
  default:
    Error(token,
      token.Is(TokenKind::Eof) ? std::string{ "unexpected end of input, expected an expression" }
                               : std::format("expected an expression, found '{}'", token.mText));
    return kNoExpr;
  }
}

} // namespace

std::unique_ptr<ViewFile>
ParseViewFile(std::string path, std::string source) noexcept
{
  auto file = std::make_unique<ViewFile>();
  file->mPath = std::move(path);
  file->mSource = std::move(source);

  Lexer lexer{ file->mSource };
  std::vector<Token> tokens;
  // Lex everything up front, which gives the parser arbitrary lookahead (needed to tell casts from parenthesized
  // expressions). An error token terminates lexing; the parser reports it when it gets there.
  do {
    tokens.push_back(lexer.Next());
  } while (!tokens.back().Is(TokenKind::Eof) && !tokens.back().Is(TokenKind::Error));
  if (tokens.back().Is(TokenKind::Error)) {
    tokens.push_back(Token{ .mKind = TokenKind::Eof, .mLoc = tokens.back().mLoc });
  }

  Parser parser{ *file, std::move(tokens) };
  parser.ParseFile();
  return file;
}

} // namespace mdb::views
