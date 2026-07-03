// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

#include <string_view>
#include <vector>

#include "ast.hpp"
#include "lexer.hpp"  // Token, Diagnostic, tokenize

// cheatah parser — tokens -> AST. Recursive descent over the v0 grammar:
//   program     := (NEWLINE | stmt)*
//   stmt        := import_stmt | expr_stmt
//   import_stmt := 'import' dotted ('as' IDENT)?
//   expr_stmt   := expr
//   expr        := primary ( '.' IDENT | '(' args ')' )*
//   primary     := STRING | NUMBER | IDENT | '(' expr ')'
namespace cheatah {

struct ParseResult {
    Program program;
    std::vector<Diagnostic> diagnostics;
    bool ok() const { return diagnostics.empty(); }
};

// Parse a token stream (as produced by tokenize()).
ParseResult parse(const std::vector<Token>& tokens);

// Convenience: lex + parse a source buffer (lexer diagnostics are merged in).
ParseResult parse_source(std::string_view source);

} // namespace cheatah
