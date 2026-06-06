#include "lexer.hpp"

#include <array>
#include <algorithm>
#include <cctype>

namespace cheatah {

const char* to_string(TokenKind kind) {
    switch (kind) {
        case TokenKind::Number:     return "Number";
        case TokenKind::String:     return "String";
        case TokenKind::Identifier: return "Identifier";
        case TokenKind::Keyword:    return "Keyword";
        case TokenKind::LParen:     return "LParen";
        case TokenKind::RParen:     return "RParen";
        case TokenKind::LBrace:     return "LBrace";
        case TokenKind::RBrace:     return "RBrace";
        case TokenKind::LBracket:   return "LBracket";
        case TokenKind::RBracket:   return "RBracket";
        case TokenKind::Comma:      return "Comma";
        case TokenKind::Colon:      return "Colon";
        case TokenKind::Semicolon:  return "Semicolon";
        case TokenKind::Dot:        return "Dot";
        case TokenKind::Assign:     return "Assign";
        case TokenKind::Plus:       return "Plus";
        case TokenKind::Minus:      return "Minus";
        case TokenKind::Star:       return "Star";
        case TokenKind::Slash:      return "Slash";
        case TokenKind::Caret:      return "Caret";
        case TokenKind::Power:        return "Power";
        case TokenKind::EqualEqual:   return "EqualEqual";
        case TokenKind::BangEqual:    return "BangEqual";
        case TokenKind::Less:         return "Less";
        case TokenKind::LessEqual:    return "LessEqual";
        case TokenKind::Greater:      return "Greater";
        case TokenKind::GreaterEqual: return "GreaterEqual";
        case TokenKind::Newline:    return "Newline";
        case TokenKind::EndOfInput: return "EndOfInput";
        case TokenKind::Invalid:    return "Invalid";
    }
    return "Invalid";
}

bool is_keyword(std::string_view word) {
    // General-purpose language keywords (cheatah is no longer plotting-only).
    // Builtins like print / show / show_window are runtime FUNCTIONS, not keywords.
    // Sorted for binary_search — keep alphabetical and in sync with README.md.
    static constexpr std::array<std::string_view, 20> kKeywords{
        "and", "as", "else", "except", "false", "fn", "for", "from", "if",
        "import", "in", "let", "not", "or", "raise", "return", "struct",
        "true", "try", "while",
    };
    return std::binary_search(kKeywords.begin(), kKeywords.end(), word);
}

namespace {

bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool is_ident_continue(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}
bool is_digit(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

// Single-pass scanner over the source buffer, tracking 1-based line/column.
class Scanner {
public:
    explicit Scanner(std::string_view src) : src_(src) {}

    LexResult run() {
        while (!at_end()) {
            scan_token();
        }
        push(TokenKind::EndOfInput, "");
        return std::move(result_);
    }

private:
    bool at_end() const { return pos_ >= src_.size(); }
    char peek() const { return at_end() ? '\0' : src_[pos_]; }
    char peek_next() const { return (pos_ + 1 >= src_.size()) ? '\0' : src_[pos_ + 1]; }

    char advance() {
        const char c = src_[pos_++];
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return c;
    }

    SourcePos here() const { return {line_, column_}; }

    void push(TokenKind kind, std::string text, SourcePos at) {
        result_.tokens.push_back({kind, std::move(text), at});
    }
    void push(TokenKind kind, std::string text) { push(kind, std::move(text), here()); }

    void error(std::string message, SourcePos at) {
        result_.diagnostics.push_back({std::move(message), at});
    }

    void scan_token() {
        const SourcePos start = here();
        const char c = peek();

        // Whitespace (not newline).
        if (c == ' ' || c == '\t' || c == '\r') {
            advance();
            return;
        }
        // Newline is a significant token.
        if (c == '\n') {
            advance();
            push(TokenKind::Newline, "\n", start);
            return;
        }
        // Comments: `#…` or `//…` to end of line.
        if (c == '#' || (c == '/' && peek_next() == '/')) {
            while (!at_end() && peek() != '\n') {
                advance();
            }
            return;
        }
        if (is_digit(c) || (c == '.' && is_digit(peek_next()))) {
            scan_number(start);
            return;
        }
        if (is_ident_start(c)) {
            scan_identifier(start);
            return;
        }
        if (c == '"') {
            scan_string(start);
            return;
        }
        scan_symbol(start);
    }

    void scan_number(SourcePos start) {
        const std::size_t begin = pos_;
        while (is_digit(peek())) {
            advance();
        }
        if (peek() == '.' && is_digit(peek_next())) {
            advance();  // '.'
            while (is_digit(peek())) {
                advance();
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            const char sign = peek_next();
            if (is_digit(sign) || ((sign == '+' || sign == '-') && is_digit(peek_next2()))) {
                advance();  // e/E
                if (peek() == '+' || peek() == '-') {
                    advance();
                }
                while (is_digit(peek())) {
                    advance();
                }
            }
        }
        push(TokenKind::Number, std::string(src_.substr(begin, pos_ - begin)), start);
    }

    char peek_next2() const { return (pos_ + 2 >= src_.size()) ? '\0' : src_[pos_ + 2]; }

    void scan_identifier(SourcePos start) {
        const std::size_t begin = pos_;
        while (is_ident_continue(peek())) {
            advance();
        }
        std::string_view word = src_.substr(begin, pos_ - begin);
        push(is_keyword(word) ? TokenKind::Keyword : TokenKind::Identifier,
             std::string(word), start);
    }

    void scan_string(SourcePos start) {
        advance();  // opening quote
        std::string value;
        while (!at_end() && peek() != '"') {
            const char c = advance();
            if (c == '\n') {
                error("unterminated string literal", start);
                push(TokenKind::Invalid, std::move(value), start);
                return;
            }
            if (c == '\\') {
                if (at_end()) {
                    break;
                }
                const char esc = advance();
                switch (esc) {
                    case 'n': value.push_back('\n'); break;
                    case 't': value.push_back('\t'); break;
                    case '"': value.push_back('"'); break;
                    case '\\': value.push_back('\\'); break;
                    default:
                        error(std::string("unknown escape '\\") + esc + "'", here());
                        value.push_back(esc);
                        break;
                }
            } else {
                value.push_back(c);
            }
        }
        if (at_end()) {
            error("unterminated string literal", start);
            push(TokenKind::Invalid, std::move(value), start);
            return;
        }
        advance();  // closing quote
        push(TokenKind::String, std::move(value), start);
    }

    void scan_symbol(SourcePos start) {
        const char c = advance();
        switch (c) {
            case '(': push(TokenKind::LParen, "(", start); return;
            case ')': push(TokenKind::RParen, ")", start); return;
            case '{': push(TokenKind::LBrace, "{", start); return;
            case '}': push(TokenKind::RBrace, "}", start); return;
            case '[': push(TokenKind::LBracket, "[", start); return;
            case ']': push(TokenKind::RBracket, "]", start); return;
            case ',': push(TokenKind::Comma, ",", start); return;
            case ':': push(TokenKind::Colon, ":", start); return;
            case ';': push(TokenKind::Semicolon, ";", start); return;
            case '.': push(TokenKind::Dot, ".", start); return;
            case '=':
                if (peek() == '=') { advance(); push(TokenKind::EqualEqual, "==", start); }
                else { push(TokenKind::Assign, "=", start); }
                return;
            case '!':
                if (peek() == '=') { advance(); push(TokenKind::BangEqual, "!=", start); return; }
                error("unexpected character '!' (did you mean '!=' or 'not'?)", start);
                push(TokenKind::Invalid, "!", start);
                return;
            case '<':
                if (peek() == '=') { advance(); push(TokenKind::LessEqual, "<=", start); }
                else { push(TokenKind::Less, "<", start); }
                return;
            case '>':
                if (peek() == '=') { advance(); push(TokenKind::GreaterEqual, ">=", start); }
                else { push(TokenKind::Greater, ">", start); }
                return;
            case '+': push(TokenKind::Plus, "+", start); return;
            case '-': push(TokenKind::Minus, "-", start); return;
            case '*':
                if (peek() == '*') { advance(); push(TokenKind::Power, "**", start); }
                else { push(TokenKind::Star, "*", start); }
                return;
            case '/': push(TokenKind::Slash, "/", start); return;
            case '^': push(TokenKind::Caret, "^", start); return;
            default:
                error(std::string("unexpected character '") + c + "'", start);
                push(TokenKind::Invalid, std::string(1, c), start);
                return;
        }
    }

    std::string_view src_;
    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 1;
    LexResult result_;
};

} // namespace

LexResult tokenize(std::string_view source) {
    return Scanner(source).run();
}

} // namespace cheatah::plotting::script
