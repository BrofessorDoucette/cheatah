#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// cheatah tokens — the output of the lexer and the input to the (future)
// parser. See README.md for the language's lexical structure.
namespace cheatah {

enum class TokenKind {
    // Literals.
    Number,      // 1, 3.14, 1e-3
    String,      // "text" (Token::text holds the DECODED value, escapes resolved)
    Identifier,  // foo, spot, K_1
    Keyword,     // a reserved word (see is_keyword)
    CppBlock,    // raw C++ from a `cpp { … }` escape hatch (text = the verbatim body)

    // Punctuation.
    LParen, RParen,      // ( )
    LBrace, RBrace,      // { }
    LBracket, RBracket,  // [ ]
    Comma, Colon, Semicolon, Dot,
    Assign,              // =

    // Operators.
    Plus, Minus, Star, Slash, FloorDiv, Caret, Power,  // + - * / // ^ **
    EqualEqual, BangEqual,            // == !=
    Less, LessEqual, Greater, GreaterEqual,  // < <= > >=

    // Structural.
    Newline,      // statement separator
    EndOfInput,   // always the final token

    // Error sentinel (paired with a Diagnostic).
    Invalid,
};

// 1-based source location, for diagnostics and editor tooling.
struct SourcePos {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

struct Token {
    TokenKind kind = TokenKind::Invalid;
    std::string text;   // lexeme; for String this is the decoded value
    SourcePos pos;      // position of the lexeme's first character
};

} // namespace cheatah
