#include "parser.hpp"

namespace cheatah {

namespace {

class Parser {
public:
    explicit Parser(const std::vector<Token>& toks) : toks_(toks) {}

    ParseResult run() {
        ParseResult r;
        r.program.body = parse_stmts(/*in_block=*/false);
        r.diagnostics = std::move(diags_);
        return r;
    }

private:
    // ---- token cursor ----
    const Token& peek() const { return toks_[pos_]; }
    const Token& prev() const { return toks_[pos_ - 1]; }
    bool at_end() const { return peek().kind == TokenKind::EndOfInput; }
    bool check(TokenKind k) const { return peek().kind == k; }
    bool check_kw(std::string_view kw) const {
        return peek().kind == TokenKind::Keyword && peek().text == kw;
    }
    const Token& advance() {
        if (!at_end()) ++pos_;
        return prev();
    }
    void error(const std::string& msg) { diags_.push_back({msg, peek().pos}); }
    void skip_newlines() {
        while (check(TokenKind::Newline)) advance();
    }
    void synchronize() {  // recover to the next line / block boundary
        while (!at_end() && !check(TokenKind::Newline) && !check(TokenKind::RBrace)) advance();
    }

    // ---- statement lists / blocks ----
    Block parse_stmts(bool in_block) {
        Block body;
        skip_newlines();
        while (!at_end() && !(in_block && check(TokenKind::RBrace))) {
            StmtPtr s = parse_stmt();
            if (s) body.push_back(std::move(s));
            if (!at_end() && !check(TokenKind::Newline) && !check(TokenKind::RBrace)) {
                error("expected end of line after statement");
                synchronize();
            }
            skip_newlines();
        }
        return body;
    }

    Block parse_block() {
        Block body;
        if (!check(TokenKind::LBrace)) {
            error("expected '{'");
            return body;
        }
        advance();  // {
        body = parse_stmts(/*in_block=*/true);
        if (!check(TokenKind::RBrace)) {
            error("expected '}'");
        } else {
            advance();  // }
        }
        return body;
    }

    StmtPtr parse_stmt() {
        if (check(TokenKind::CppBlock)) return std::make_unique<RawCpp>(advance().text);
        if (check_kw("import")) return parse_import();
        if (check_kw("struct")) return parse_struct();
        if (check_kw("fn")) return parse_fn();
        if (check_kw("let")) return parse_let();
        if (check_kw("if")) return parse_if();
        if (check_kw("while")) return parse_while();
        if (check_kw("for")) return parse_for();
        if (check_kw("return")) return parse_return();
        if (check_kw("try")) return parse_try();
        if (check_kw("raise")) return parse_raise();
        return parse_assign_or_expr();
    }

    StmtPtr parse_import() {
        advance();  // import
        auto imp = std::make_unique<Import>();
        if (!check(TokenKind::Identifier)) {
            error("expected a module name after 'import'");
            synchronize();
            return nullptr;
        }
        imp->module.push_back(advance().text);
        while (check(TokenKind::Dot)) {
            advance();
            if (!check(TokenKind::Identifier)) {
                error("expected a name after '.' in the module path");
                synchronize();
                return nullptr;
            }
            imp->module.push_back(advance().text);
        }
        if (check_kw("as")) {
            advance();
            if (!check(TokenKind::Identifier)) {
                error("expected an alias name after 'as'");
                synchronize();
                return nullptr;
            }
            imp->alias = advance().text;
        }
        return imp;
    }

    StmtPtr parse_struct() {
        advance();  // struct
        auto s = std::make_unique<StructDef>();
        if (!check(TokenKind::Identifier)) {
            error("expected a struct name");
            synchronize();
            return nullptr;
        }
        s->name = advance().text;
        if (!check(TokenKind::LBrace)) {
            error("expected '{' after struct name");
            synchronize();
            return nullptr;
        }
        advance();  // {
        skip_newlines();
        while (!at_end() && !check(TokenKind::RBrace)) {
            if (!check(TokenKind::Identifier)) {
                error("expected a field name");
                synchronize();
                skip_newlines();
                continue;
            }
            Field f;
            f.name = advance().text;
            if (!check(TokenKind::Colon)) {
                error("expected ':' after field name");
                synchronize();
                skip_newlines();
                continue;
            }
            advance();  // :
            if (!check(TokenKind::Identifier)) {
                error("expected a field type");
                synchronize();
                skip_newlines();
                continue;
            }
            f.type = parse_type();
            s->fields.push_back(std::move(f));
            if (check(TokenKind::Comma)) advance();
            skip_newlines();
        }
        if (!check(TokenKind::RBrace)) {
            error("expected '}' to close struct");
        } else {
            advance();
        }
        return s;
    }

    StmtPtr parse_fn() {
        advance();  // fn
        auto f = std::make_unique<FnDef>();
        if (!check(TokenKind::Identifier)) {
            error("expected a function name");
            synchronize();
            return nullptr;
        }
        f->name = advance().text;
        if (!check(TokenKind::LParen)) {
            error("expected '(' after function name");
            synchronize();
            return nullptr;
        }
        advance();  // (
        if (!check(TokenKind::RParen)) {
            for (;;) {
                if (!check(TokenKind::Identifier)) {
                    error("expected a parameter name");
                    break;
                }
                f->params.push_back(advance().text);
                if (check(TokenKind::Comma)) {
                    advance();
                    continue;
                }
                break;
            }
        }
        if (!check(TokenKind::RParen)) {
            error("expected ')' after parameters");
            synchronize();
            return nullptr;
        }
        advance();  // )
        f->body = parse_block();
        return f;
    }

    StmtPtr parse_let() {
        advance();  // let
        auto l = std::make_unique<Let>();
        if (!check(TokenKind::Identifier)) {
            error("expected a name after 'let'");
            synchronize();
            return nullptr;
        }
        l->name = advance().text;
        if (!check(TokenKind::Assign)) {
            error("expected '=' after the name in 'let'");
            synchronize();
            return nullptr;
        }
        advance();  // =
        l->value = parse_expr();
        if (!l->value) {
            synchronize();
            return nullptr;
        }
        return l;
    }

    StmtPtr parse_if() {
        advance();  // if
        auto n = std::make_unique<If>();
        n->cond = parse_expr();
        if (!n->cond) {
            synchronize();
            return nullptr;
        }
        n->then_body = parse_block();
        if (check_kw("else")) {
            advance();
            if (check_kw("if")) {
                StmtPtr nested = parse_if();  // else if …
                if (nested) n->else_body.push_back(std::move(nested));
            } else {
                n->else_body = parse_block();
            }
        }
        return n;
    }

    StmtPtr parse_while() {
        advance();  // while
        auto n = std::make_unique<While>();
        n->cond = parse_expr();
        if (!n->cond) {
            synchronize();
            return nullptr;
        }
        n->body = parse_block();
        return n;
    }

    StmtPtr parse_for() {
        advance();  // for
        auto n = std::make_unique<For>();
        if (!check(TokenKind::Identifier)) {
            error("expected a loop variable after 'for'");
            synchronize();
            return nullptr;
        }
        n->var = advance().text;
        if (!check_kw("in")) {
            error("expected 'in' after the loop variable");
            synchronize();
            return nullptr;
        }
        advance();  // in
        n->iterable = parse_expr();
        if (!n->iterable) {
            synchronize();
            return nullptr;
        }
        n->body = parse_block();
        return n;
    }

    StmtPtr parse_return() {
        advance();  // return
        auto n = std::make_unique<Return>();
        if (!check(TokenKind::Newline) && !check(TokenKind::RBrace) && !at_end()) {
            n->value = parse_expr();
        }
        return n;
    }

    StmtPtr parse_try() {
        advance();  // try
        auto t = std::make_unique<Try>();
        t->body = parse_block();
        if (!check_kw("except")) {
            error("expected 'except' after a try block");
            return t;
        }
        advance();  // except
        if (check(TokenKind::Identifier)) {
            t->catch_var = advance().text;  // bind the error message
        }
        t->catch_body = parse_block();
        return t;
    }

    StmtPtr parse_raise() {
        advance();  // raise
        auto r = std::make_unique<Raise>();
        r->value = parse_expr();
        if (!r->value) {
            synchronize();
            return nullptr;
        }
        return r;
    }

    StmtPtr parse_assign_or_expr() {
        ExprPtr e = parse_expr();
        if (!e) {
            synchronize();
            return nullptr;
        }
        if (check(TokenKind::Assign)) {
            advance();
            auto a = std::make_unique<Assign>();
            a->target = std::move(e);
            a->value = parse_expr();
            if (!a->value) {
                synchronize();
                return nullptr;
            }
            return a;
        }
        return std::make_unique<ExprStmt>(std::move(e));
    }

    // type := IDENT ( '[' (type | NUMBER) (',' (type | NUMBER))* ']' )?
    TypeRef parse_type() {
        TypeRef t;
        if (!check(TokenKind::Identifier)) {
            error("expected a type name");
            return t;
        }
        t.name = advance().text;
        if (check(TokenKind::LBracket)) {
            advance();
            for (;;) {
                if (check(TokenKind::Number)) {
                    t.array_size = advance().text;  // array[T, N]
                } else if (check(TokenKind::Identifier)) {
                    t.args.push_back(parse_type());
                } else {
                    error("expected a type or size inside '[...]'");
                    break;
                }
                if (check(TokenKind::Comma)) {
                    advance();
                    continue;
                }
                break;
            }
            if (check(TokenKind::RBracket)) {
                advance();
            } else {
                error("expected ']' to close the type");
            }
        }
        return t;
    }

    // ---- expressions (precedence climbing) ----
    ExprPtr parse_expr() { return parse_or(); }

    ExprPtr parse_or() {
        ExprPtr e = parse_and();
        while (e && check_kw("or")) {
            advance();
            ExprPtr r = parse_and();
            e = std::make_unique<Binary>("||", std::move(e), std::move(r));
        }
        return e;
    }
    ExprPtr parse_and() {
        ExprPtr e = parse_not();
        while (e && check_kw("and")) {
            advance();
            ExprPtr r = parse_not();
            e = std::make_unique<Binary>("&&", std::move(e), std::move(r));
        }
        return e;
    }
    ExprPtr parse_not() {
        if (check_kw("not")) {
            advance();
            ExprPtr o = parse_not();
            return o ? std::make_unique<Unary>("!", std::move(o)) : nullptr;
        }
        return parse_comparison();
    }
    ExprPtr parse_comparison() {
        ExprPtr e = parse_additive();
        for (;;) {
            std::string op;
            if (check(TokenKind::EqualEqual)) op = "==";
            else if (check(TokenKind::BangEqual)) op = "!=";
            else if (check(TokenKind::Less)) op = "<";
            else if (check(TokenKind::LessEqual)) op = "<=";
            else if (check(TokenKind::Greater)) op = ">";
            else if (check(TokenKind::GreaterEqual)) op = ">=";
            else break;
            advance();
            ExprPtr r = parse_additive();
            e = std::make_unique<Binary>(op, std::move(e), std::move(r));
        }
        return e;
    }
    ExprPtr parse_additive() {
        ExprPtr e = parse_multiplicative();
        for (;;) {
            std::string op;
            if (check(TokenKind::Plus)) op = "+";
            else if (check(TokenKind::Minus)) op = "-";
            else break;
            advance();
            ExprPtr r = parse_multiplicative();
            e = std::make_unique<Binary>(op, std::move(e), std::move(r));
        }
        return e;
    }
    ExprPtr parse_multiplicative() {
        ExprPtr e = parse_unary();
        for (;;) {
            std::string op;
            if (check(TokenKind::Star)) op = "*";
            else if (check(TokenKind::Slash)) op = "/";
            else if (check(TokenKind::Caret)) op = "^";
            else break;
            advance();
            ExprPtr r = parse_unary();
            e = std::make_unique<Binary>(op, std::move(e), std::move(r));
        }
        return e;
    }
    ExprPtr parse_unary() {
        if (check(TokenKind::Minus)) {
            advance();
            ExprPtr o = parse_unary();
            return o ? std::make_unique<Unary>("-", std::move(o)) : nullptr;
        }
        return parse_power();
    }
    ExprPtr parse_power() {  // `**` binds tighter than unary minus, right-associative
        ExprPtr base = parse_postfix();
        if (base && check(TokenKind::Power)) {
            advance();
            ExprPtr exp = parse_unary();  // right side may be -exp and recurses (right-assoc)
            return std::make_unique<Binary>("**", std::move(base), std::move(exp));
        }
        return base;
    }

    ExprPtr parse_postfix() {
        ExprPtr e = parse_primary();
        if (!e) return nullptr;
        for (;;) {
            if (check(TokenKind::Dot)) {
                advance();
                if (!check(TokenKind::Identifier)) {
                    error("expected a name after '.'");
                    return nullptr;
                }
                e = std::make_unique<Member>(std::move(e), advance().text);
            } else if (check(TokenKind::LParen)) {
                advance();
                std::vector<ExprPtr> args = parse_args();
                if (!check(TokenKind::RParen)) {
                    error("expected ')' after arguments");
                    return nullptr;
                }
                advance();
                e = std::make_unique<Call>(std::move(e), std::move(args));
            } else if (check(TokenKind::LBracket)) {
                advance();
                ExprPtr idx = parse_expr();
                if (!check(TokenKind::RBracket)) {
                    error("expected ']' after index");
                    return nullptr;
                }
                advance();
                e = std::make_unique<Index>(std::move(e), std::move(idx));
            } else {
                break;
            }
        }
        return e;
    }

    std::vector<ExprPtr> parse_args() {
        std::vector<ExprPtr> args;
        if (check(TokenKind::RParen)) return args;
        for (;;) {
            ExprPtr a = parse_expr();
            if (!a) break;
            args.push_back(std::move(a));
            if (check(TokenKind::Comma)) {
                advance();
                continue;
            }
            break;
        }
        return args;
    }

    ExprPtr parse_primary() {
        if (check(TokenKind::String)) return std::make_unique<StringLit>(advance().text);
        if (check(TokenKind::Number)) return std::make_unique<NumberLit>(advance().text);
        if (check_kw("true")) {
            advance();
            return std::make_unique<BoolLit>(true);
        }
        if (check_kw("false")) {
            advance();
            return std::make_unique<BoolLit>(false);
        }
        if (check(TokenKind::Identifier)) return std::make_unique<Ident>(advance().text);
        if (check(TokenKind::LBracket)) return parse_list_literal();
        if (check(TokenKind::LBrace)) return parse_dict_literal();
        if (check(TokenKind::LParen)) {
            advance();
            ExprPtr e = parse_expr();
            if (!check(TokenKind::RParen)) {
                error("expected ')'");
                return nullptr;
            }
            advance();
            return e;
        }
        error("expected an expression");
        return nullptr;
    }

    ExprPtr parse_list_literal() {
        advance();  // [
        auto lst = std::make_unique<ListLit>();
        skip_newlines();
        if (!check(TokenKind::RBracket)) {
            for (;;) {
                ExprPtr e = parse_expr();
                if (!e) break;
                lst->elements.push_back(std::move(e));
                if (check(TokenKind::Comma)) {
                    advance();
                    skip_newlines();
                    continue;
                }
                break;
            }
        }
        skip_newlines();
        if (!check(TokenKind::RBracket)) {
            error("expected ']' to close the list");
            return nullptr;
        }
        advance();
        return lst;
    }

    ExprPtr parse_dict_literal() {
        advance();  // {
        auto d = std::make_unique<DictLit>();
        skip_newlines();
        if (!check(TokenKind::RBrace)) {
            for (;;) {
                ExprPtr k = parse_expr();
                if (!k) break;
                if (!check(TokenKind::Colon)) {
                    error("expected ':' in dict entry");
                    return nullptr;
                }
                advance();
                ExprPtr v = parse_expr();
                if (!v) break;
                d->keys.push_back(std::move(k));
                d->values.push_back(std::move(v));
                if (check(TokenKind::Comma)) {
                    advance();
                    skip_newlines();
                    continue;
                }
                break;
            }
        }
        skip_newlines();
        if (!check(TokenKind::RBrace)) {
            error("expected '}' to close the dict");
            return nullptr;
        }
        advance();
        return d;
    }

    const std::vector<Token>& toks_;
    std::size_t pos_ = 0;
    std::vector<Diagnostic> diags_;
};

} // namespace

ParseResult parse(const std::vector<Token>& tokens) { return Parser(tokens).run(); }

ParseResult parse_source(std::string_view source) {
    const LexResult lex = tokenize(source);
    ParseResult r = parse(lex.tokens);
    r.diagnostics.insert(r.diagnostics.begin(), lex.diagnostics.begin(), lex.diagnostics.end());
    return r;
}

} // namespace cheatah
