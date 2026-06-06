#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "token.hpp"

// cheatah lexer — turns source text into a token stream. The first stage of
// the language pipeline (lexer -> parser -> codegen). Pure and dependency-free so
// it is fully unit-testable in isolation.
namespace cheatah {

// A lexical error (e.g. unterminated string, stray character). The lexer recovers
// and keeps scanning, so a single pass surfaces every error.
struct Diagnostic {
    std::string message;
    SourcePos pos;
};

struct LexResult {
    std::vector<Token> tokens;            // always terminated by an EndOfInput token
    std::vector<Diagnostic> diagnostics;  // empty when the input is lexically clean

    bool ok() const { return diagnostics.empty(); }
};

// Tokenize a cheatah source buffer. Never throws.
LexResult tokenize(std::string_view source);

// True if `word` is a reserved cheatah keyword (lexed as TokenKind::Keyword).
bool is_keyword(std::string_view word);

} // namespace cheatah
