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
    // Statement separators: newlines and (optional) semicolons. A `;` lets you put
    // several statements on one line or end one C-style; it is never required. (Only
    // used where statements live — list/dict literals still separate with commas.)
    void skip_separators() {
        while (check(TokenKind::Newline) || check(TokenKind::Semicolon)) advance();
    }
    void synchronize() {  // recover to the next statement / block boundary
        while (!at_end() && !check(TokenKind::Newline) && !check(TokenKind::Semicolon) &&
               !check(TokenKind::RBrace))
            advance();
    }

    // ---- statement lists / blocks ----
    Block parse_stmts(bool in_block) {
        Block body;
        skip_separators();
        while (!at_end() && !(in_block && check(TokenKind::RBrace))) {
            const std::size_t before = pos_;
            StmtPtr s = parse_stmt();
            if (s) body.push_back(std::move(s));
            if (!at_end() && !check(TokenKind::Newline) && !check(TokenKind::Semicolon) &&
                !check(TokenKind::RBrace)) {
                error("expected a newline or ';' after the statement");
                synchronize();
            }
            // Guarantee forward progress: a failed parse that didn't consume anything
            // (e.g. a stray top-level `}` — synchronize() stops AT an RBrace, and the
            // top-level loop doesn't exit on one) would otherwise spin forever. Skip
            // the offending token so a malformed program ERRORS rather than hanging.
            if (pos_ == before && !at_end()) {
                error("unexpected token");
                advance();
            }
            skip_separators();
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
        const unsigned ln = peek().pos.line;  // the statement's first token
        StmtPtr s = parse_stmt_inner();
        if (s && s->line == 0) s->line = ln;  // for #line -> .purr-mapped diagnostics
        return s;
    }

    StmtPtr parse_stmt_inner() {
        if (check(TokenKind::CppBlock)) return std::make_unique<RawCpp>(advance().text);
        if (check_kw("import")) return parse_import();
        if (check_kw("interface")) return parse_interface();
        if (check_kw("enum")) return parse_enum();
        if (check_kw("struct")) return parse_struct();
        if (check_kw("fn")) return parse_fn();
        if (check_kw("let")) return parse_let();
        if (check_kw("if")) return parse_if();
        if (check_kw("while")) return parse_while();
        if (check_kw("for")) return parse_for();
        if (check_kw("return")) return parse_return();
        if (check_kw("try")) return parse_try();
        if (check_kw("raise")) return parse_raise();
        if (check_kw("break")) { advance(); return std::make_unique<Break>(); }
        if (check_kw("continue")) { advance(); return std::make_unique<Continue>(); }
        if (check_kw("match")) return parse_match();
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
        if (check(TokenKind::Colon)) {  // : Iface1, Iface2  — interfaces this struct fulfills
            advance();
            for (;;) {
                if (!check(TokenKind::Identifier)) {
                    error("expected an interface name after ':'");
                    break;
                }
                s->fulfills.push_back(advance().text);
                if (check(TokenKind::Comma)) {
                    advance();
                    continue;
                }
                break;
            }
        }
        if (!check(TokenKind::LBrace)) {
            error("expected '{' after struct name");
            synchronize();
            return nullptr;
        }
        advance();  // {
        skip_separators();
        while (!at_end() && !check(TokenKind::RBrace)) {
            if (check_kw("fn")) {  // a method: fn name(self, …) { … }
                StmtPtr m = parse_fn();
                if (m) s->methods.push_back(std::move(m));
                skip_separators();
                continue;
            }
            if (!check(TokenKind::Identifier)) {
                error("expected a field name or 'fn'");
                synchronize();
                skip_separators();
                continue;
            }
            Field f;
            f.name = advance().text;
            if (!check(TokenKind::Colon)) {
                error("expected ':' after field name");
                synchronize();
                skip_separators();
                continue;
            }
            advance();  // :
            if (!check(TokenKind::Identifier)) {
                error("expected a field type");
                synchronize();
                skip_separators();
                continue;
            }
            f.type = parse_type();
            s->fields.push_back(std::move(f));
            if (check(TokenKind::Comma) || check(TokenKind::Semicolon)) advance();
            skip_separators();
        }
        if (!check(TokenKind::RBrace)) {
            error("expected '}' to close struct");
        } else {
            advance();
        }
        return s;
    }

    // enum Name { A [= expr], B, … } — a scoped enumeration (-> C++ `enum class`).
    // Members are separated by newlines, commas, or semicolons; each may carry an
    // optional `= <expr>` value.
    StmtPtr parse_enum() {
        advance();  // enum
        auto e = std::make_unique<EnumDef>();
        if (!check(TokenKind::Identifier)) {
            error("expected an enum name");
            synchronize();
            return nullptr;
        }
        e->name = advance().text;
        if (!check(TokenKind::LBrace)) {
            error("expected '{' after enum name");
            synchronize();
            return nullptr;
        }
        advance();  // {
        skip_separators();
        while (!at_end() && !check(TokenKind::RBrace)) {
            if (!check(TokenKind::Identifier)) {
                error("expected an enum member name");
                synchronize();
                skip_separators();
                continue;
            }
            Enumerator en;
            en.name = advance().text;
            if (check(TokenKind::Assign)) {  // optional explicit value: A = 1
                advance();
                en.value = parse_expr();
            }
            e->enumerators.push_back(std::move(en));
            if (check(TokenKind::Comma) || check(TokenKind::Semicolon)) advance();
            skip_separators();
        }
        if (!check(TokenKind::RBrace)) {
            error("expected '}' to close enum");
        } else {
            advance();
        }
        return e;
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
                // Optional `: Type` — an interface name here constrains the param.
                std::string ptype;
                if (check(TokenKind::Colon)) {
                    advance();
                    if (check(TokenKind::Identifier)) {
                        ptype = advance().text;
                    } else {
                        error("expected a type after ':' in the parameter list");
                    }
                }
                f->param_types.push_back(ptype);
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

    StmtPtr parse_interface() {
        advance();  // interface
        auto it = std::make_unique<InterfaceDef>();
        if (!check(TokenKind::Identifier)) {
            error("expected an interface name");
            synchronize();
            return nullptr;
        }
        it->name = advance().text;
        if (!check(TokenKind::LBrace)) {
            error("expected '{' after the interface name");
            synchronize();
            return nullptr;
        }
        advance();  // {
        skip_separators();
        while (!at_end() && !check(TokenKind::RBrace)) {
            if (!check_kw("fn")) {
                error("expected a method signature ('fn …') inside the interface");
                synchronize();
                skip_separators();
                continue;
            }
            advance();  // fn
            InterfaceMethod m;
            if (!check(TokenKind::Identifier)) {
                error("expected a method name");
                synchronize();
                skip_separators();
                continue;
            }
            m.name = advance().text;
            if (!check(TokenKind::LParen)) {
                error("expected '(' after the method name");
                synchronize();
                skip_separators();
                continue;
            }
            advance();  // (
            bool first = true;
            if (!check(TokenKind::RParen)) {
                for (;;) {
                    if (!check(TokenKind::Identifier)) {
                        error("expected a parameter name");
                        break;
                    }
                    advance();  // param name (the first is the `self` receiver)
                    if (first) {
                        first = false;  // self has no declared type
                    } else if (check(TokenKind::Colon)) {
                        advance();
                        m.param_types.push_back(parse_type());
                    } else {
                        TypeRef any;  // untyped interface param -> no type constraint
                        m.param_types.push_back(any);
                    }
                    if (check(TokenKind::Comma)) {
                        advance();
                        continue;
                    }
                    break;
                }
            }
            if (!check(TokenKind::RParen)) {
                error("expected ')' after the method parameters");
                synchronize();
                skip_separators();
                continue;
            }
            advance();  // )
            it->methods.push_back(std::move(m));
            skip_separators();
        }
        if (!check(TokenKind::RBrace)) {
            error("expected '}' to close the interface");
        } else {
            advance();
        }
        return it;
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
        if (check(TokenKind::Colon)) {  // optional `: <type>` annotation
            advance();
            l->type = parse_type();
            l->has_type = true;
        }
        // The initializer is OPTIONAL: `let x` (or `let x: T`) declares a variable with no
        // value yet — it must be given one before use. Codegen realizes it at its first
        // assignment, removes it if it is never assigned, and the build fails if it is used
        // without (or only conditionally) being given a value. l->value stays null here.
        if (check(TokenKind::Assign)) {
            advance();  // =
            l->value = parse_expr();
            if (!l->value) {
                synchronize();
                return nullptr;
            }
        }
        return l;
    }

    StmtPtr parse_if() {
        advance();  // if
        return parse_if_rest();
    }
    // The cond+block of an if, plus any `elif`/`else if`/`else` tail. Shared by the
    // leading `if` and each `elif` so chains nest as If-in-else_body.
    StmtPtr parse_if_rest() {
        auto n = std::make_unique<If>();
        n->cond = parse_expr();
        if (!n->cond) {
            synchronize();
            return nullptr;
        }
        n->then_body = parse_block();
        // `elif`/`else` may sit on the next line(s) (`} \n elif …`), so skip
        // separators to look for one — but only consume them if a continuation
        // actually follows, otherwise the newline correctly ends the `if` statement.
        const std::size_t after_block = pos_;
        skip_separators();
        if (!check_kw("elif") && !check_kw("else")) {
            pos_ = after_block;
        }
        if (check_kw("elif")) {
            advance();  // elif — like `else if`, condition follows directly
            StmtPtr nested = parse_if_rest();
            if (nested) n->else_body.push_back(std::move(nested));
        } else if (check_kw("else")) {
            advance();
            if (check_kw("if")) {
                advance();
                StmtPtr nested = parse_if_rest();  // else if …
                if (nested) n->else_body.push_back(std::move(nested));
            } else {
                n->else_body = parse_block();
            }
        }
        return n;
    }

    StmtPtr parse_match() {
        advance();  // match
        auto m = std::make_unique<Match>();
        m->subject = parse_expr();
        if (!m->subject) {
            synchronize();
            return nullptr;
        }
        if (!check(TokenKind::LBrace)) {
            error("expected '{' after the match subject");
            synchronize();
            return nullptr;
        }
        advance();  // {
        skip_separators();
        while (!at_end() && !check(TokenKind::RBrace)) {
            if (!check_kw("case")) {
                error("expected 'case' inside a match");
                synchronize();
                skip_separators();
                continue;
            }
            advance();  // case
            MatchCase c;
            // `case _ { … }` is the default; otherwise `case <expr> { … }`.
            if (check(TokenKind::Identifier) && peek().text == "_") {
                advance();
                c.wildcard = true;
            } else {
                c.pattern = parse_expr();
                if (!c.pattern) {
                    synchronize();
                    skip_separators();
                    continue;
                }
            }
            c.body = parse_block();
            m->cases.push_back(std::move(c));
            skip_separators();
        }
        if (!check(TokenKind::RBrace)) {
            error("expected '}' to close the match");
        } else {
            advance();
        }
        return m;
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
        if (!check(TokenKind::Newline) && !check(TokenKind::Semicolon) &&
            !check(TokenKind::RBrace) && !at_end()) {
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
            else if (check(TokenKind::Slash)) op = "/";        // true (float) division
            else if (check(TokenKind::FloorDiv)) op = "//";    // floor division
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
                // `obj[i]` (index) or `obj[a:b]` (slice); either bound may be omitted.
                ExprPtr first;
                if (!check(TokenKind::Colon)) {
                    first = parse_expr();
                    if (!first) return nullptr;
                }
                if (check(TokenKind::Colon)) {
                    advance();
                    ExprPtr last;
                    if (!check(TokenKind::RBracket)) {
                        last = parse_expr();
                        if (!last) return nullptr;
                    }
                    if (!check(TokenKind::RBracket)) {
                        error("expected ']' after slice");
                        return nullptr;
                    }
                    advance();
                    e = std::make_unique<Slice>(std::move(e), std::move(first), std::move(last));
                } else {
                    if (!first) {
                        error("expected an index expression");
                        return nullptr;
                    }
                    if (!check(TokenKind::RBracket)) {
                        error("expected ']' after index");
                        return nullptr;
                    }
                    advance();
                    e = std::make_unique<Index>(std::move(e), std::move(first));
                }
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
        if (check(TokenKind::LBrace)) {
            // `{ .field = … }` is a struct designated initializer; `{ key: … }` is a dict.
            // The leading `.` after `{` disambiguates them.
            if (pos_ + 1 < toks_.size() && toks_[pos_ + 1].kind == TokenKind::Dot)
                return parse_struct_init();
            return parse_dict_literal();
        }
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
        const std::uint32_t open_line = peek().pos.line;  // the '[' line
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
        advance();  // ]
        // A literal whose `[`…`]` spanned multiple source lines stays multi-line in the
        // generated C++ (readable .gen.cpp), since the lexer drops newlines inside brackets.
        lst->multiline = prev().pos.line != open_line;
        return lst;
    }

    // `{ .field = value, … }` — a C++20-style designated initializer for a struct. Used as
    // the argument of a struct call, `Type({ .f = v })`. Fields not listed default-initialize.
    ExprPtr parse_struct_init() {
        const std::uint32_t open_line = peek().pos.line;  // the '{' line
        advance();  // {
        auto si = std::make_unique<StructInit>();
        skip_newlines();
        while (!check(TokenKind::RBrace) && !at_end()) {
            if (!check(TokenKind::Dot)) {
                error("expected '.field = value' in a struct initializer");
                return nullptr;
            }
            advance();  // .
            if (!check(TokenKind::Identifier)) {
                error("expected a field name after '.' in a struct initializer");
                return nullptr;
            }
            std::string field = advance().text;
            if (!check(TokenKind::Assign)) {
                error("expected '=' after '." + field + "' in a struct initializer");
                return nullptr;
            }
            advance();  // =
            ExprPtr v = parse_expr();
            if (!v) return nullptr;
            si->fields.push_back(std::move(field));
            si->values.push_back(std::move(v));
            skip_newlines();
            if (check(TokenKind::Comma)) {
                advance();
                skip_newlines();
                continue;
            }
            break;
        }
        skip_newlines();
        if (!check(TokenKind::RBrace)) {
            error("expected '}' to close the struct initializer");
            return nullptr;
        }
        advance();  // }
        si->multiline = prev().pos.line != open_line;
        return si;
    }

    ExprPtr parse_dict_literal() {
        const std::uint32_t open_line = peek().pos.line;  // the '{' line
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
        advance();  // }
        d->multiline = prev().pos.line != open_line;  // keep a multi-line dict multi-line
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
