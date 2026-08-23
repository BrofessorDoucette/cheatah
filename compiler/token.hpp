// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// cheatah tokens — the output of the lexer and the input to the (future)
// parser. See README.md for the language's lexical structure.
namespace cheatah {

enum class TokenKind : std::uint8_t {
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
    PlusAssign, MinusAssign, StarAssign, SlashAssign,  // += -= *= /= (compound assignment)

    // Operators.
    Plus, Minus, Star, Slash, FloorDiv, Caret, Power,  // + - * / // ^ **
    Percent,                                           // %  (Python floor-mod semantics)
    Ampersand,                                         // &  (unary address-of, passed through to C++
                                                       //    verbatim — for interfacing with C APIs)
    EqualEqual, BangEqual,            // == !=
    Less, LessEqual, Greater, GreaterEqual,  // < <= > >=
    Arrow,                            // ->  (function return-type hint)

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
