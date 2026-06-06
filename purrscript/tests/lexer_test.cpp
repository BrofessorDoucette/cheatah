#include "lexer.hpp"

#include <vector>

#include <gtest/gtest.h>

using cheatah::purrscript::Diagnostic;
using cheatah::purrscript::LexResult;
using cheatah::purrscript::TokenKind;
using cheatah::purrscript::tokenize;

namespace {

// Kinds of every token except the trailing EndOfInput, for compact assertions.
std::vector<TokenKind> kinds(const LexResult& r) {
    std::vector<TokenKind> out;
    for (const auto& t : r.tokens) {
        if (t.kind == TokenKind::EndOfInput) {
            break;
        }
        out.push_back(t.kind);
    }
    return out;
}

} // namespace

TEST(PurrscriptLexer, EmptyInputIsJustEndOfInput) {
    const LexResult r = tokenize("");
    ASSERT_EQ(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens.front().kind, TokenKind::EndOfInput);
    EXPECT_TRUE(r.ok());
}

TEST(PurrscriptLexer, AlwaysTerminatedByEndOfInput) {
    const LexResult r = tokenize("let x = 1");
    ASSERT_FALSE(r.tokens.empty());
    EXPECT_EQ(r.tokens.back().kind, TokenKind::EndOfInput);
}

TEST(PurrscriptLexer, ClassifiesKeywordsVsIdentifiers) {
    // `let` and `fn` are keywords; `meow` and `print` are identifiers (print is a
    // runtime builtin function, not a language keyword).
    const LexResult r = tokenize("let meow = fn");
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(kinds(r), (std::vector<TokenKind>{
                            TokenKind::Keyword,     // let
                            TokenKind::Identifier,  // meow
                            TokenKind::Assign,      // =
                            TokenKind::Keyword,     // fn
                        }));
    EXPECT_EQ(r.tokens[0].text, "let");
    EXPECT_EQ(r.tokens[1].text, "meow");
}

TEST(PurrscriptLexer, PrintIsAnIdentifierNotAKeyword) {
    const LexResult r = tokenize("print(\"meow\")");
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(kinds(r), (std::vector<TokenKind>{
                            TokenKind::Identifier,  // print
                            TokenKind::LParen,
                            TokenKind::String,      // "meow"
                            TokenKind::RParen,
                        }));
    EXPECT_EQ(r.tokens[2].text, "meow");
}

TEST(PurrscriptLexer, ImportKeywords) {
    // `from`, `import`, `as` are keywords; the module/name/alias are identifiers.
    const LexResult r = tokenize("from io import print as p");
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(kinds(r), (std::vector<TokenKind>{
                            TokenKind::Keyword,     // from
                            TokenKind::Identifier,  // io
                            TokenKind::Keyword,     // import
                            TokenKind::Identifier,  // print
                            TokenKind::Keyword,     // as
                            TokenKind::Identifier,  // p
                        }));
}

TEST(PurrscriptLexer, ScansNumbersIncludingFloatAndExponent) {
    const LexResult r = tokenize("1 3.14 4.20 1e-3 2.5E6");
    EXPECT_TRUE(r.ok());
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(r.tokens[i].kind, TokenKind::Number) << "token " << i;
    }
    EXPECT_EQ(r.tokens[0].text, "1");
    EXPECT_EQ(r.tokens[3].text, "1e-3");
    EXPECT_EQ(r.tokens[4].text, "2.5E6");
}

TEST(PurrscriptLexer, DecodesStringEscapes) {
    const LexResult r = tokenize(R"("meow\n\"purr\"")");
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(r.tokens.front().kind, TokenKind::String);
    EXPECT_EQ(r.tokens.front().text, "meow\n\"purr\"");
}

TEST(PurrscriptLexer, LexesDotForMemberAccess) {
    const LexResult r = tokenize("io.print");
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(kinds(r), (std::vector<TokenKind>{
                            TokenKind::Identifier,  // io
                            TokenKind::Dot,
                            TokenKind::Identifier,  // print
                        }));
}

TEST(PurrscriptLexer, SkipsHashAndSlashComments) {
    const LexResult r = tokenize("let a = 1  # trailing\n// whole line\nreturn");
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(kinds(r), (std::vector<TokenKind>{
                            TokenKind::Keyword,     // let
                            TokenKind::Identifier,  // a
                            TokenKind::Assign,      // =
                            TokenKind::Number,      // 1
                            TokenKind::Newline,
                            TokenKind::Newline,
                            TokenKind::Keyword,     // return
                        }));
}

TEST(PurrscriptLexer, LexesPunctuationAndOperators) {
    const LexResult r = tokenize("(){}[],:;+-*/^");
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(kinds(r), (std::vector<TokenKind>{
                            TokenKind::LParen, TokenKind::RParen,
                            TokenKind::LBrace, TokenKind::RBrace,
                            TokenKind::LBracket, TokenKind::RBracket,
                            TokenKind::Comma, TokenKind::Colon, TokenKind::Semicolon,
                            TokenKind::Plus, TokenKind::Minus, TokenKind::Star,
                            TokenKind::Slash, TokenKind::Caret,
                        }));
}

TEST(PurrscriptLexer, TracksLineAndColumn) {
    const LexResult r = tokenize("let\n  x");
    EXPECT_EQ(r.tokens[0].pos.line, 1u);
    EXPECT_EQ(r.tokens[0].pos.column, 1u);
    const auto& x = r.tokens[2];  // [0]=let, [1]=Newline, [2]=x
    EXPECT_EQ(x.kind, TokenKind::Identifier);
    EXPECT_EQ(x.pos.line, 2u);
    EXPECT_EQ(x.pos.column, 3u);
}

TEST(PurrscriptLexer, ReportsUnterminatedString) {
    const LexResult r = tokenize("\"oops");
    EXPECT_FALSE(r.ok());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_NE(r.diagnostics.front().message.find("unterminated"), std::string::npos);
}

TEST(PurrscriptLexer, RecoversAfterStrayCharacterAndKeepsScanning) {
    const LexResult r = tokenize("a ? b");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokenKind::Identifier);
    EXPECT_EQ(r.tokens[0].text, "a");
    EXPECT_EQ(r.tokens[2].kind, TokenKind::Identifier);
    EXPECT_EQ(r.tokens[2].text, "b");
}
