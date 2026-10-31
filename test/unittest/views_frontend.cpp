#include <gtest/gtest.h>
#include <views/builtin_types.h>
#include <views/lexer.h>
#include <views/parser.h>
#include <views/text.h>

using namespace mdb;
using namespace mdb::views;

static std::vector<Token>
LexAll(std::string_view source)
{
  Lexer lexer{ source };
  std::vector<Token> tokens;
  do {
    tokens.push_back(lexer.Next());
  } while (!tokens.back().Is(TokenKind::Eof) && !tokens.back().Is(TokenKind::Error));
  return tokens;
}

TEST(ViewsLexer, NumbersStringsAndComments)
{
  const auto tokens = LexAll(R"(0x1F 1'000 42u 3.5 "a\"b" 'c' // comment
    /* block */ -> :: ... >> <=)");
  ASSERT_EQ(tokens.size(), 12);
  EXPECT_EQ(tokens[0].mKind, TokenKind::Integer);
  EXPECT_EQ(tokens[0].mInteger, 0x1F);
  EXPECT_EQ(tokens[1].mInteger, 1000);
  EXPECT_EQ(tokens[2].mInteger, 42);
  EXPECT_EQ(tokens[3].mKind, TokenKind::Float);
  EXPECT_DOUBLE_EQ(tokens[3].mFloat, 3.5);
  EXPECT_EQ(tokens[4].mKind, TokenKind::String);
  EXPECT_EQ(tokens[4].mText, R"("a\"b")");
  EXPECT_EQ(tokens[5].mKind, TokenKind::Char);
  EXPECT_EQ(tokens[6].mKind, TokenKind::Arrow);
  EXPECT_EQ(tokens[7].mKind, TokenKind::ColonColon);
  EXPECT_EQ(tokens[8].mKind, TokenKind::Ellipsis);
  EXPECT_EQ(tokens[9].mKind, TokenKind::GreaterGreater);
  EXPECT_EQ(tokens[10].mKind, TokenKind::LessEqual);
  EXPECT_EQ(tokens[6].mLoc.mLine, 2);
  EXPECT_EQ(tokens.back().mKind, TokenKind::Eof);
}

TEST(ViewsLexer, Errors)
{
  EXPECT_EQ(LexAll(R"("unterminated)").back().mKind, TokenKind::Error);
  EXPECT_EQ(LexAll("/* never closed").back().mKind, TokenKind::Error);
  EXPECT_EQ(LexAll("@").back().mKind, TokenKind::Error);
  // Exponents are not supported in float literals
  EXPECT_EQ(LexAll("1e3").back().mKind, TokenKind::Error);
}

TEST(ViewsLexer, StatementOperators)
{
  const auto tokens = LexAll("++ -- += -= a--b a - -b");
  ASSERT_EQ(tokens.size(), 12);
  EXPECT_EQ(tokens[0].mKind, TokenKind::PlusPlus);
  EXPECT_EQ(tokens[1].mKind, TokenKind::MinusMinus);
  EXPECT_EQ(tokens[2].mKind, TokenKind::PlusEqual);
  EXPECT_EQ(tokens[3].mKind, TokenKind::MinusEqual);
  EXPECT_EQ(tokens[4].mText, "a");
  EXPECT_EQ(tokens[5].mKind, TokenKind::MinusMinus);
  EXPECT_EQ(tokens[6].mText, "b");
  EXPECT_EQ(tokens[7].mText, "a");
  EXPECT_EQ(tokens[8].mKind, TokenKind::Minus);
  EXPECT_EQ(tokens[9].mKind, TokenKind::Minus);
  EXPECT_EQ(tokens[10].mText, "b");
  EXPECT_EQ(TokenKindName(TokenKind::PlusPlus), "'++'");
  EXPECT_EQ(TokenKindName(TokenKind::MinusMinus), "'--'");
  EXPECT_EQ(TokenKindName(TokenKind::PlusEqual), "'+='");
  EXPECT_EQ(TokenKindName(TokenKind::MinusEqual), "'-='");
}

static std::unique_ptr<ViewFile>
Parse(std::string source)
{
  return ParseViewFile("test.mdbv", std::move(source));
}

TEST(ViewsParser, FullView)
{
  auto file = Parse(R"(
    // An nsTArray stores its elements right after its header
    view nsTArray<.*> in "libxul.so" {
      using T = template_arg(0)
      let hdr = mHdr;
      summary "length={hdr->mLength}"
      children array((T*)(hdr + 1), hdr->mLength)
    }
  )");
  ASSERT_TRUE(file->mDiagnostics.empty()) << file->mDiagnostics[0].ToString();
  ASSERT_EQ(file->mViews.size(), 1);
  const ViewDefinition &view = file->mViews[0];
  EXPECT_EQ(view.mPattern.mSource, "nsTArray<.*>");
  EXPECT_EQ(view.mPattern.mLoc.mLine, 3);
  ASSERT_TRUE(view.mObjectFile.has_value());
  EXPECT_EQ(*view.mObjectFile, "libxul.so");

  ASSERT_EQ(view.mUsings.size(), 1);
  EXPECT_EQ(view.mUsings[0].mName, "T");
  EXPECT_EQ(view.mUsings[0].mKind, UsingBinding::Kind::TemplateArgument);
  EXPECT_EQ(view.mUsings[0].mIndex, 0);
  ASSERT_EQ(view.mLets.size(), 1);
  EXPECT_EQ(view.mLets[0].mName, "hdr");
  EXPECT_EQ(file->mArena.Get(view.mLets[0].mExpr).mKind, ExprKind::Identifier);
  // Declarations keep their order
  ASSERT_EQ(view.mDeclarations.size(), 2);
  EXPECT_EQ(view.mDeclarations[0].mKind, Declaration::Kind::Using);
  EXPECT_EQ(view.mDeclarations[1].mKind, Declaration::Kind::Let);

  ASSERT_EQ(view.mSummary.mKind, SummarySpec::Kind::Template);
  ASSERT_EQ(view.mSummary.mParts.size(), 1);
  EXPECT_EQ(view.mSummary.mParts[0].mLiteral, "length=");
  const Expr &arrow = file->mArena.Get(view.mSummary.mParts[0].mExpr);
  EXPECT_EQ(arrow.mKind, ExprKind::Arrow);
  EXPECT_EQ(arrow.mName, "mLength");
  // Positions of expressions inside the template point into the file
  EXPECT_EQ(arrow.mLoc.mLine, 6);

  ASSERT_EQ(view.mChildren.mKind, ChildrenSpec::Kind::Expression);
  const Expr &call = file->mArena.Get(view.mChildren.mExpr);
  ASSERT_EQ(call.mKind, ExprKind::Call);
  EXPECT_EQ(call.mName, "array");
  const auto args = file->mArena.Args(call);
  ASSERT_EQ(args.size(), 2);
  // `(T*)` is a cast because T was declared by `using`
  const Expr &cast = file->mArena.Get(args[0].mExpr);
  ASSERT_EQ(cast.mKind, ExprKind::Cast);
  EXPECT_EQ(file->mArena.mTypes[cast.mType].Spelling(), "T *");
  EXPECT_EQ(file->mArena.Get(cast.mA).mKind, ExprKind::Binary);
}

TEST(ViewsParser, Patterns)
{
  auto file = Parse(R"(
    view std::vector<.*> {}
    view mozilla::detail::nsTStringRepr<(char|char16_t)> in "libxul.so" {}
    view ns[CS]?String{3} {}
    view "\(anonymous namespace\)::Hidden" {}
    view Plain { children none }
  )");
  ASSERT_TRUE(file->mDiagnostics.empty()) << file->mDiagnostics[0].ToString();
  ASSERT_EQ(file->mViews.size(), 5);
  EXPECT_EQ(file->mViews[0].mPattern.mSource, "std::vector<.*>");
  EXPECT_EQ(file->mViews[1].mPattern.mSource, "mozilla::detail::nsTStringRepr<(char|char16_t)>");
  // Braces in a pattern are regex quantifiers; the pattern ends at whitespace
  EXPECT_EQ(file->mViews[2].mPattern.mSource, "ns[CS]?String{3}");
  // Quoted patterns can contain spaces, and backslashes are regex escapes
  EXPECT_EQ(file->mViews[3].mPattern.mSource, R"(\(anonymous namespace\)::Hidden)");
  EXPECT_EQ(file->mViews[4].mChildren.mKind, ChildrenSpec::Kind::None);

  // Patterns must match the whole name
  const auto &vector = file->mViews[0].mPattern;
  EXPECT_TRUE(vector.Matches("std::vector<int,std::allocator<int>>"));
  EXPECT_FALSE(vector.Matches("std::vector<int,std::allocator<int>>::iterator"));
  EXPECT_FALSE(vector.Matches("mystd::vector<int>"));
  EXPECT_FALSE(vector.Matches("std::vector"));
  const auto &strings = file->mViews[1].mPattern;
  EXPECT_TRUE(strings.Matches("mozilla::detail::nsTStringRepr<char16_t>"));
  EXPECT_FALSE(strings.Matches("mozilla::detail::nsTStringRepr<int>"));
  EXPECT_TRUE(file->mViews[3].mPattern.Matches("(anonymous namespace)::Hidden"));
  EXPECT_TRUE(file->mViews[4].mPattern.Matches("Plain"));
  EXPECT_FALSE(file->mViews[4].mPattern.Matches("Plainer"));
}

TEST(ViewsParser, ViewIsAnOrdinaryNameInsideABody)
{
  auto file = Parse(R"(
    view Outer {
      let view = mView;
      summary "{view.mX}"
    }
  )");
  ASSERT_TRUE(file->mDiagnostics.empty()) << file->mDiagnostics[0].ToString();
  ASSERT_EQ(file->mViews.size(), 1);
  EXPECT_EQ(file->mViews[0].mLets[0].mName, "view");
}

TEST(ViewsParser, Usings)
{
  auto file = Parse(R"(
    view V {
      using Header = mozilla::Header;
      using Element = typeof(*mBegin);
      using Pointer = Element*;
      let a = (Element*)mData;
      let b = (Pointer)mData;
      let c = sizeof(Header) + template_arg(1);
    }
  )");
  ASSERT_TRUE(file->mDiagnostics.empty()) << file->mDiagnostics[0].ToString();
  const auto &view = file->mViews[0];
  ASSERT_EQ(view.mUsings.size(), 3);
  EXPECT_EQ(view.mUsings[0].mKind, UsingBinding::Kind::Type);
  EXPECT_EQ(file->mArena.mTypes[view.mUsings[0].mType].Spelling(), "mozilla::Header");
  EXPECT_EQ(view.mUsings[1].mKind, UsingBinding::Kind::TypeOf);
  EXPECT_EQ(file->mArena.Get(view.mUsings[1].mExpr).Unary(), UnaryOp::Deref);
  EXPECT_EQ(file->mArena.mTypes[view.mUsings[2].mType].Spelling(), "Element *");
  const auto kindOf = [&](size_t i) { return file->mArena.Get(view.mLets[i].mExpr).mKind; };
  EXPECT_EQ(kindOf(0), ExprKind::Cast);
  EXPECT_EQ(kindOf(1), ExprKind::Cast);
  const Expr &sum = file->mArena.Get(view.mLets[2].mExpr);
  EXPECT_EQ(file->mArena.Get(sum.mA).mKind, ExprKind::SizeofType);
  EXPECT_EQ(file->mArena.Get(sum.mB).mKind, ExprKind::Call);
}

TEST(ViewsParser, CastsVersusParentheses)
{
  auto file = Parse(R"(
    view V {
      using T = template_arg(0)
      let a = (mLength) + 1;
      let b = (T*)mData;
      let c = (Header*)mPtr;
      let d = (unsigned long)mLength;
      let e = (T)mValue;
      let f = sizeof(T) * sizeof(mLength);
      let g = (ns::Type*)mPtr;
    }
  )");
  ASSERT_TRUE(file->mDiagnostics.empty()) << file->mDiagnostics[0].ToString();
  const auto &lets = file->mViews[0].mLets;
  const auto kindOf = [&](size_t i) { return file->mArena.Get(lets[i].mExpr).mKind; };
  EXPECT_EQ(kindOf(0), ExprKind::Binary);
  EXPECT_EQ(kindOf(1), ExprKind::Cast);
  EXPECT_EQ(kindOf(2), ExprKind::Cast);
  EXPECT_EQ(kindOf(3), ExprKind::Cast);
  EXPECT_EQ(file->mArena.mTypes[file->mArena.Get(lets[3].mExpr).mType].Spelling(), "unsigned long");
  EXPECT_EQ(kindOf(4), ExprKind::Cast);
  const Expr &sizes = file->mArena.Get(lets[5].mExpr);
  EXPECT_EQ(file->mArena.Get(sizes.mA).mKind, ExprKind::SizeofType);
  EXPECT_EQ(file->mArena.Get(sizes.mB).mKind, ExprKind::SizeofExpr);
  EXPECT_EQ(kindOf(6), ExprKind::Cast);
  EXPECT_EQ(file->mArena.mTypes[file->mArena.Get(lets[6].mExpr).mType].Spelling(), "ns::Type *");
}

TEST(ViewsParser, Precedence)
{
  auto file = Parse("view V { summary a + b * c == d && !e ? f : g }");
  ASSERT_TRUE(file->mDiagnostics.empty()) << file->mDiagnostics[0].ToString();
  const auto &arena = file->mArena;
  const Expr &ternary = arena.Get(file->mViews[0].mSummary.mExpr);
  ASSERT_EQ(ternary.mKind, ExprKind::Ternary);
  const Expr &logicalAnd = arena.Get(ternary.mA);
  ASSERT_EQ(logicalAnd.Binary(), BinaryOp::LogicalAnd);
  const Expr &equal = arena.Get(logicalAnd.mA);
  ASSERT_EQ(equal.Binary(), BinaryOp::Equal);
  const Expr &add = arena.Get(equal.mA);
  ASSERT_EQ(add.Binary(), BinaryOp::Add);
  EXPECT_EQ(arena.Get(add.mB).Binary(), BinaryOp::Mul);
  EXPECT_EQ(arena.Get(logicalAnd.mB).Unary(), UnaryOp::LogicalNot);
}

TEST(ViewsParser, SummaryTemplates)
{
  auto file = Parse(R"(view V { summary "{{literal}} \"q\" x={mX,x} y={mY}" })");
  ASSERT_TRUE(file->mDiagnostics.empty()) << file->mDiagnostics[0].ToString();
  const auto &parts = file->mViews[0].mSummary.mParts;
  ASSERT_EQ(parts.size(), 2);
  EXPECT_EQ(parts[0].mLiteral, "{literal} \"q\" x=");
  EXPECT_EQ(parts[0].mFormat, FormatSpec::Hex);
  EXPECT_EQ(parts[1].mLiteral, " y=");
  EXPECT_EQ(parts[1].mFormat, FormatSpec::Default);

  auto literalOnly = Parse(R"(view V { summary "just text" })");
  ASSERT_EQ(literalOnly->mViews[0].mSummary.mParts.size(), 1);
  EXPECT_EQ(literalOnly->mViews[0].mSummary.mParts[0].mExpr, kNoExpr);

  auto expression = Parse(R"(view V { summary text(mData, mLength, limit: 10) })");
  ASSERT_TRUE(expression->mDiagnostics.empty()) << expression->mDiagnostics[0].ToString();
  const Expr &call = expression->mArena.Get(expression->mViews[0].mSummary.mExpr);
  const auto args = expression->mArena.Args(call);
  ASSERT_EQ(args.size(), 3);
  EXPECT_EQ(args[2].mName, "limit");
}

TEST(ViewsParser, ErrorRecoveryKeepsOtherViews)
{
  auto file = Parse(R"(view Good1 { summary "ok" }
view Bad {
  summary mFoo +
}
view Good2 { children none }
view AlsoBad { summary "{unterminated" }
view Good3 {}
)");
  ASSERT_EQ(file->mViews.size(), 3);
  EXPECT_EQ(file->mViews[0].mPattern.mSource, "Good1");
  EXPECT_EQ(file->mViews[1].mPattern.mSource, "Good2");
  EXPECT_EQ(file->mViews[2].mPattern.mSource, "Good3");
  ASSERT_EQ(file->mDiagnostics.size(), 2);
  EXPECT_EQ(file->mDiagnostics[0].mLoc.mLine, 4);
  EXPECT_EQ(file->mDiagnostics[1].mLoc.mLine, 6);
  EXPECT_NE(file->mDiagnostics[1].mMessage.find("unterminated"), std::string::npos);
}

TEST(ViewsParser, Errors)
{
  const auto errorOf = [](std::string source) {
    auto file = Parse(std::move(source));
    return file->mDiagnostics.empty() ? std::string{} : file->mDiagnostics[0].mMessage;
  };
  EXPECT_NE(errorOf("view V { summary 1 summary 2 }").find("only have one summary"), std::string::npos);
  EXPECT_NE(errorOf("view V { let a = 1; let a = 2; }").find("already defined"), std::string::npos);
  EXPECT_NE(errorOf("view V { using a = int; let a = 2; }").find("already defined"), std::string::npos);
  EXPECT_NE(errorOf("view V { using T = template_arg(x) }").find("index"), std::string::npos);
  EXPECT_NE(errorOf("view V { bogus }").find("expected 'let'"), std::string::npos);
  EXPECT_NE(errorOf("view V { summary f(a: 1, 2) }").find("positional"), std::string::npos);
  EXPECT_NE(errorOf("view V<(unclosed {}").find("invalid type pattern"), std::string::npos);
  EXPECT_NE(errorOf("view V{ }").find("put a space"), std::string::npos);
  EXPECT_NE(errorOf("view { }").find("expected a type pattern"), std::string::npos);
}

TEST(ViewsParser, StatementsAndChildrenBlocks)
{
  auto file = Parse(R"(
    view V {
      using T = template_arg(0)
      children {
        auto a = n;
        T *p = (T*)mData;
        p = p;
        p += 1;
        p -= 1;
        ++n;
        n++;
        --n;
        n--;
        if (n) { continue; } else break;
        while (n) n -= 1;
        for (u32 i = 0; i < n; ++i) yield [i] *p;
        for (; n; ) { return; }
        return n + 1;
        walk(p);
        yield *p;
      }
    }
    view Empty { children {} }
  )");
  ASSERT_TRUE(file->mDiagnostics.empty()) << file->mDiagnostics[0].ToString();
  ASSERT_EQ(file->mViews.size(), 2);

  const auto &arena = file->mArena;
  const auto &children = file->mViews[0].mChildren;
  EXPECT_EQ(children.mKind, ChildrenSpec::Kind::Block);
  const Stmt &body = arena.GetStmt(children.mBody);
  ASSERT_EQ(body.mKind, StmtKind::Block);
  const auto bodyItems = arena.BlockItems(body);
  ASSERT_EQ(bodyItems.size(), 16);
  EXPECT_EQ(arena.GetStmt(bodyItems[0]).mKind, StmtKind::Declare);
  EXPECT_EQ(arena.GetStmt(bodyItems[1]).mKind, StmtKind::Declare);
  EXPECT_EQ(arena.GetStmt(bodyItems[2]).mKind, StmtKind::Assign);
  EXPECT_EQ(arena.GetStmt(bodyItems[3]).mOperator, AssignOp::Add);
  EXPECT_EQ(arena.GetStmt(bodyItems[4]).mOperator, AssignOp::Sub);
  EXPECT_EQ(arena.GetStmt(bodyItems[5]).mKind, StmtKind::Increment);
  EXPECT_EQ(arena.GetStmt(bodyItems[9]).mKind, StmtKind::If);
  EXPECT_EQ(arena.GetStmt(bodyItems[10]).mKind, StmtKind::While);
  EXPECT_EQ(arena.GetStmt(bodyItems[11]).mKind, StmtKind::For);
  EXPECT_EQ(arena.GetStmt(bodyItems[12]).mKind, StmtKind::For);
  // `for (; n; )`: no init, no step
  EXPECT_EQ(arena.GetStmt(bodyItems[12]).mA, kNoStmt);
  EXPECT_EQ(arena.GetStmt(bodyItems[12]).mB, kNoStmt);
  EXPECT_EQ(arena.GetStmt(bodyItems[13]).mKind, StmtKind::Return);
  EXPECT_NE(arena.GetStmt(bodyItems[13]).mExpr, kNoExpr);
  EXPECT_EQ(arena.GetStmt(bodyItems[14]).mKind, StmtKind::Call);
  EXPECT_EQ(arena.GetStmt(bodyItems[15]).mKind, StmtKind::Yield);
  EXPECT_NE(arena.GetStmt(arena.GetStmt(bodyItems[11]).mC).mKey, kNoExpr);

  const Stmt &empty = arena.GetStmt(file->mViews[1].mChildren.mBody);
  EXPECT_TRUE(arena.BlockItems(empty).empty());
}

TEST(ViewsParser, StatementErrorsAndExpressionChildren)
{
  const auto errorOf = [](std::string source) {
    auto file = Parse(std::move(source));
    return file->mDiagnostics.empty() ? std::string{} : file->mDiagnostics[0].mMessage;
  };
  EXPECT_NE(errorOf("view V { children { auto x = 1 } }").find("expected ';'"), std::string::npos);
  EXPECT_NE(errorOf("view V { children { x + 1; } }").find("expected a statement"), std::string::npos);

  auto file = Parse("view V { children (array(p, n)) }");
  ASSERT_TRUE(file->mDiagnostics.empty()) << file->mDiagnostics[0].ToString();
  EXPECT_EQ(file->mViews[0].mChildren.mKind, ChildrenSpec::Kind::Expression);
  EXPECT_EQ(file->mArena.Get(file->mViews[0].mChildren.mExpr).mName, "array");
}

TEST(ViewsParser, StatementViewFiles)
{
  auto hashtable = Parse(R"(
    view MdbHashMap<.*> {
      using Entry = template_arg(0)
      summary "count={mTable.mEntryCount}"
      children {
        for (i64 slot = 0; slot < mTable.mCapacity; ++slot) {
          if (mTable.mHashes[slot] <= 1) continue;
          auto entry = (Entry*)(mTable.mEntries + slot * mTable.mEntrySize);
          yield *entry;
        }
      }
    }
  )");
  ASSERT_TRUE(hashtable->mDiagnostics.empty()) << hashtable->mDiagnostics[0].ToString();

  auto loops = Parse(R"(
    view MdbList {
      summary "length={mLength}"
      children {
        for (auto node = mHead; node; node = node->mNext)
          yield node->mValue;
      }
    }
    view MdbCircularList {
      children {
        for (auto link = mSentinel.mNext; link != &mSentinel; link = link->mNext)
          yield *(MdbItem*)link;
      }
    }
    view MdbForever {
      children {
        while (true) {
        }
      }
    }
  )");
  ASSERT_TRUE(loops->mDiagnostics.empty()) << loops->mDiagnostics[0].ToString();
}

TEST(ViewsParser, Functions)
{
  auto file = Parse(R"(
    view V {
      fn empty() { return; }
      fn value(int x, unsigned long &acc) -> int { return x; }
      summary fn + 1
      children walk(value)
    }
  )");
  ASSERT_TRUE(file->mDiagnostics.empty()) << file->mDiagnostics[0].ToString();
  const auto &view = file->mViews[0];
  ASSERT_EQ(view.mFunctions.size(), 2);
  EXPECT_EQ(view.mFunctions[0].mName, "empty");
  EXPECT_FALSE(view.mFunctions[0].mReturnType.has_value());
  ASSERT_EQ(view.mFunctions[1].mParams.size(), 2);
  EXPECT_EQ(view.mFunctions[1].mParams[0].mName, "x");
  EXPECT_FALSE(view.mFunctions[1].mParams[0].mIsReference);
  EXPECT_EQ(view.mFunctions[1].mParams[1].mName, "acc");
  EXPECT_TRUE(view.mFunctions[1].mParams[1].mIsReference);
  EXPECT_TRUE(view.mFunctions[1].mReturnType.has_value());
  EXPECT_EQ(view.mChildren.mKind, ChildrenSpec::Kind::Expression);

  auto duplicate = Parse("view V { fn f() {} fn f() {} }");
  ASSERT_FALSE(duplicate->mDiagnostics.empty());
  EXPECT_NE(duplicate->mDiagnostics[0].mMessage.find("function 'f' is already defined"), std::string::npos);
  auto letCollision = Parse("view V { let f = 1; fn f() {} }");
  ASSERT_FALSE(letCollision->mDiagnostics.empty());
  EXPECT_NE(letCollision->mDiagnostics[0].mMessage.find("'f' is already defined"), std::string::npos);
  auto missingName = Parse("view V { fn f(int) {} }");
  ASSERT_FALSE(missingName->mDiagnostics.empty());
  EXPECT_NE(missingName->mDiagnostics[0].mMessage.find("expected a parameter name"), std::string::npos);
  auto missingReturnType = Parse("view V { fn f() -> {} }");
  ASSERT_FALSE(missingReturnType->mDiagnostics.empty());
  EXPECT_NE(missingReturnType->mDiagnostics[0].mMessage.find("expected a type"), std::string::npos);
  auto missingBody = Parse("view V { fn f() }");
  ASSERT_FALSE(missingBody->mDiagnostics.empty());
  EXPECT_NE(
    missingBody->mDiagnostics[0].mMessage.find("expected '{' to start the function body"), std::string::npos);
}

TEST(ViewsBuiltinTypes, Lookup)
{
  const auto lookup = [](std::initializer_list<std::string_view> words) {
    return LookupBuiltinType(std::span{ words.begin(), words.size() });
  };
  EXPECT_EQ(lookup({ "unsigned", "long" })->mSize, 8);
  EXPECT_EQ(lookup({ "unsigned", "long" })->mKind, TypeKind::UnsignedInt);
  EXPECT_EQ(lookup({ "short", "unsigned", "int" })->mSize, 2);
  EXPECT_EQ(lookup({ "unsigned" })->mSize, 4);
  EXPECT_EQ(lookup({ "long", "long" })->mKind, TypeKind::SignedInt);
  EXPECT_TRUE(lookup({ "char16_t" })->mIsChar);
  EXPECT_EQ(lookup({ "u32" })->mSize, 4);
  EXPECT_EQ(lookup({ "size_t" })->mKind, TypeKind::UnsignedInt);
  EXPECT_TRUE(lookup({ "char" })->mIsChar);
  EXPECT_FALSE(lookup({ "unsigned", "signed" }).has_value());
  EXPECT_FALSE(lookup({ "short", "long" }).has_value());
  EXPECT_FALSE(lookup({ "char16_t", "int" }).has_value());
}

static std::string
Decode(std::vector<u8> bytes, u8 width, bool stop = false)
{
  std::string out;
  DecodeText(bytes, width, stop, out);
  return out;
}

static std::vector<u8>
Utf16Bytes(std::initializer_list<u16> units)
{
  std::vector<u8> bytes;
  for (const u16 unit : units) {
    bytes.push_back(unit & 0xFF);
    bytes.push_back(unit >> 8);
  }
  return bytes;
}

TEST(ViewsText, Utf16)
{
  // "Hi 😀" with a surrogate pair
  EXPECT_EQ(Decode(Utf16Bytes({ 'H', 'i', ' ', 0xD83D, 0xDE00 }), 2), "Hi \xF0\x9F\x98\x80");
  // 日本語
  EXPECT_EQ(Decode(Utf16Bytes({ 0x65E5, 0x672C, 0x8A9E }), 2), "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
  // Lone surrogates become U+FFFD
  EXPECT_EQ(Decode(Utf16Bytes({ 0xD83D, 'a' }), 2),
    "\xEF\xBF\xBD"
    "a");
  EXPECT_EQ(Decode(Utf16Bytes({ 0xDE00 }), 2), "\xEF\xBF\xBD");
  // Stops at the terminator when asked to
  std::string out;
  const auto result = DecodeText(Utf16Bytes({ 'a', 0, 'b' }), 2, true, out);
  EXPECT_EQ(out, "a");
  EXPECT_TRUE(result.mFoundTerminator);
  EXPECT_EQ(result.mUnits, 2);
}

TEST(ViewsText, Utf8AndUtf32)
{
  EXPECT_EQ(Decode({ 'C', 'a', 'f', 0xC3, 0xA9 }, 1), "Caf\xC3\xA9");
  // Truncated sequence, invalid lead byte, overlong encoding
  EXPECT_EQ(Decode({ 'a', 0xE6, 0x97 }, 1), "a\xEF\xBF\xBD");
  EXPECT_EQ(Decode({ 0xFF, 'b' }, 1),
    "\xEF\xBF\xBD"
    "b");
  EXPECT_EQ(Decode({ 0xC0, 0x80 }, 1), "\xEF\xBF\xBD");
  EXPECT_EQ(Decode({ 0x00, 0xF6, 0x01, 0x00 }, 4), "\xF0\x9F\x98\x80");
  EXPECT_EQ(Decode({ 0x00, 0x00, 0x11, 0x00 }, 4), "\xEF\xBF\xBD");
}

TEST(ViewsText, QuotingAndJson)
{
  std::string quoted;
  AppendQuoted("a\"b\\c\nd\x01", true, quoted);
  EXPECT_EQ(quoted, R"("a\"b\\c\nd\x01"...)");

  std::string json;
  AppendJsonEscaped(quoted, json);
  EXPECT_EQ(json, R"(\"a\\\"b\\\\c\\nd\\x01\"...)");

  std::string control;
  AppendJsonEscaped(std::string_view{ "\x02\t", 2 }, control);
  EXPECT_EQ(control, R"(\u0002\t)");
}
