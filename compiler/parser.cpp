// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "parser.hpp"

#include <map>
#include <span>

namespace cheatah {

namespace {

class Parser {
public:
    explicit Parser(const std::vector<Token>& toks) : toks_(toks) {}  // view: the caller keeps the tokens alive

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
    bool check_next(TokenKind k) const {
        return pos_ + 1 < toks_.size() && toks_[pos_ + 1].kind == k;
    }
    bool check_next_kw(std::string_view kw) const {
        return pos_ + 1 < toks_.size() && toks_[pos_ + 1].kind == TokenKind::Keyword &&
               toks_[pos_ + 1].text == kw;
    }
    bool check_kw(std::string_view kw) const {
        return peek().kind == TokenKind::Keyword && peek().text == kw;
    }
    // A bare identifier matching `name` — used for contextual modifiers that are NOT
    // reserved keywords (e.g. `constexpr` after `if`), so the word stays usable elsewhere.
    bool check_ident(std::string_view name) const {
        return peek().kind == TokenKind::Identifier && peek().text == name;
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
        // All keyword-led statements share one `kind == Keyword` guard, so a non-keyword
        // statement (the common case — assignments/expressions) pays a single kind check
        // instead of re-reading the token and re-testing the kind once per candidate. The
        // dispatch order and the branch each keyword takes are unchanged. `match` lives here
        // too; the contextual `constexpr <stmt>` forms below start with an IDENTIFIER, so they
        // are never reached from inside this block (kind != Keyword) — behaviour is identical.
        if (check(TokenKind::Keyword)) {
            const std::string_view kw = peek().text;
            if (kw == "import") return parse_import();
            if (kw == "interface") return parse_interface();
            if (kw == "enum") return parse_enum();
            if (kw == "struct") return parse_struct();
            if (kw == "fn") return parse_fn();
            if (kw == "let") return parse_let();
            if (kw == "if") return parse_if();
            if (kw == "while") return parse_while();
            if (kw == "for") return parse_for();
            if (kw == "with") return parse_with();
            if (kw == "return") return parse_return();
            if (kw == "try") return parse_try();
            if (kw == "raise") return parse_raise();
            if (kw == "break") { advance(); return std::make_unique<Break>(); }
            if (kw == "continue") { advance(); return std::make_unique<Continue>(); }
            if (kw == "match") return parse_match(false);
            // Any other keyword (and/or/not/in/…) is not a statement head — fall through.
        }
        // `constexpr let …` / `constexpr match …` — the contextual `constexpr` modifier
        // (an identifier, not a keyword) only when immediately followed by the statement it
        // qualifies, so the word stays free for ordinary use elsewhere.
        if (check_ident("constexpr") && check_next_kw("fn")) {
            advance();  // constexpr
            return parse_fn(/*is_constexpr=*/true);
        }
        if (check_ident("constexpr") && check_next_kw("let")) {
            advance();  // constexpr
            return parse_let(/*is_constexpr=*/true);
        }
        if (check_ident("constexpr") && check_next_kw("match")) {
            advance();  // constexpr
            return parse_match(/*is_constexpr=*/true);
        }
        if (check_kw("match")) return parse_match(false);
        return parse_assign_or_expr();
    }

    StmtPtr parse_import() {
        advance();  // import
        auto imp = std::make_unique<Import>();
        if (!check(TokenKind::Identifier)) {
            error("expected a module or symbol name after 'import'");
            synchronize();
            return nullptr;
        }
        const std::string first = advance().text;

        // `import <sym>[, <sym>…] from <module.path>` — a from-import binds the named symbols directly,
        // usable without the module prefix. Detected by a following `,` or `from`.
        if (check_kw("from") || check(TokenKind::Comma)) {
            imp->symbols.push_back(first);
            while (check(TokenKind::Comma)) {
                advance();
                if (!check(TokenKind::Identifier)) {
                    error("expected a symbol name after ','");
                    synchronize();
                    return nullptr;
                }
                imp->symbols.push_back(advance().text);
            }
            if (!check_kw("from")) {
                error("expected 'from' after the imported symbols");
                synchronize();
                return nullptr;
            }
            advance();  // from
            if (!check(TokenKind::Identifier)) {
                error("expected a module name after 'from'");
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
            return imp;
        }

        // `import <module.path> [as <alias>]` — the classic module import.
        imp->module.push_back(first);
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
                const unsigned ln = peek().pos.line;
                StmtPtr m = parse_fn();
                if (m) {
                    if (m->line == 0) m->line = ln;  // methods bypass parse_stmt's stamping
                    s->methods.push_back(std::move(m));
                }
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
            f.line = peek().pos.line;  // for doc-comment attachment (attach_docs)
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

    StmtPtr parse_fn(bool is_constexpr = false) {
        advance();  // fn
        auto f = std::make_unique<FnDef>();
        f->is_constexpr = is_constexpr;
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
                // Optional `const` modifier: `const name : T` lowers the param to a `const&`
                // (read-only), so e.g. a model's `predict(self, const x : ndarray<float>)` can satisfy
                // a `const Array&` C++ concept. Contextual (not a reserved word) — `const` is the
                // modifier only when a parameter name follows it, so it stays usable as an identifier.
                bool param_const = false;
                if (check_ident("const") && check_next(TokenKind::Identifier)) {
                    advance();  // const
                    param_const = true;
                }
                f->params.push_back(advance().text);
                // Optional `: Type` — an interface name here constrains the param;
                // `: ndarray<float>` style generics carry their element type, and nested
                // generics (`: list<ndarray<float>>`) are supported via parse_type_string.
                std::string ptype;
                if (check(TokenKind::Colon)) {
                    advance();
                    if (check(TokenKind::Identifier)) {
                        ptype = parse_type_string();
                    } else {
                        error("expected a type after ':' in the parameter list");
                    }
                }
                // Encode `const` into the type spelling so codegen emits a const reference.
                if (param_const && !ptype.empty()) ptype.insert(0, "const ");
                f->param_types.push_back(ptype);
                // Optional `= <expr>` — a default value. Once one parameter has a default,
                // every later one must too (they lower to trailing forwarding overloads).
                ExprPtr dflt;
                if (check(TokenKind::Assign)) {
                    advance();
                    dflt = parse_expr();
                } else if (!f->param_defaults.empty() && f->param_defaults.back()) {
                    error("parameter '" + f->params.back() +
                          "' without a default may not follow a defaulted parameter");
                }
                f->param_defaults.push_back(std::move(dflt));
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
        // Optional Python-style return-type hint: `fn f(...) -> Type { … }`. When present the
        // codegen emits it as the concrete C++ return type, so the C++ backend enforces that
        // every `return` matches. Absent -> `auto` (the abbreviated-template default).
        if (check(TokenKind::Arrow)) {
            advance();
            f->return_type = parse_type_string();
        }
        f->body = parse_block();
        return f;
    }

    // A type in the param/return STRING spelling: `Name`, `Name<arg, ...>`, NESTED
    // (`list<ndarray<float>>`), or MODULE-QUALIFIED (`state.State`, `memory.Memory` — a type
    // reached through an import alias). Kept as a string so the codegen maps it the same way (the
    // codegen resolves the leading module alias exactly as it does for a `state.State()` call). The
    // type arguments recurse, so a generic-of-a-generic parses (the lexer emits `>>` as two Greater
    // tokens, which the recursion closes one level at a time).
    std::string parse_type_string() {
        std::string t;
        if (!check(TokenKind::Identifier)) {
            error("expected a type name");
            return t;
        }
        t = advance().text;
        // Module-qualified path: `alias.Type` (or deeper). Consume each `.Name` into the spelling.
        while (check(TokenKind::Dot)) {
            advance();  // .
            if (!check(TokenKind::Identifier)) {
                error("expected a type name after '.'");
                break;
            }
            t += ".";
            t += advance().text;
        }
        if (check(TokenKind::Less)) {
            t += advance().text;  // '<'
            for (;;) {
                if (check(TokenKind::Number)) {
                    // A NON-TYPE template argument — a compile-time size, e.g. the extents in
                    // `fixarray.Fixed<f32, 4, 4>` or `Store<float, 1024>`. Spliced verbatim;
                    // map_type_string emits a pure-number spelling as-is.
                    t += advance().text;
                } else if (check(TokenKind::Identifier)) {
                    t += parse_type_string();  // recurse: nested generics like ndarray<float>
                } else {
                    error("expected a type or size inside '<...>'");
                    break;
                }
                if (check(TokenKind::Comma)) { t += advance().text; continue; }
                break;
            }
            if (check(TokenKind::Greater)) t += advance().text;
            else error("expected '>' to close the type arguments");
        }
        return t;
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

    StmtPtr parse_let(bool is_constexpr = false) {
        advance();  // let
        auto l = std::make_unique<Let>();
        l->is_constexpr = is_constexpr;
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

    // `constexpr` (a contextual modifier, not a keyword) right after `if`/`else if`,
    // gated on a following `(` so a plain `if constexpr > 0 {…}` still reads `constexpr`
    // as an ordinary identifier. The C++-style parens are the condition's own grouping.
    bool eat_constexpr_modifier() {
        if (check_ident("constexpr") && check_next(TokenKind::LParen)) {
            advance();  // constexpr
            return true;
        }
        return false;
    }

    StmtPtr parse_if() {
        advance();  // if
        return parse_if_rest(eat_constexpr_modifier());
    }
    // The cond+block of an if, plus any `elif`/`else if`/`else` tail. Shared by the
    // leading `if` and each `elif` so chains nest as If-in-else_body. `is_constexpr`
    // is threaded down the whole chain so one leading `if constexpr` makes every arm a
    // compile-time branch (`else if constexpr` may also be written out explicitly).
    StmtPtr parse_if_rest(bool is_constexpr) {
        auto n = std::make_unique<If>();
        n->is_constexpr = is_constexpr;
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
            StmtPtr nested = parse_if_rest(is_constexpr);
            if (nested) n->else_body.push_back(std::move(nested));
        } else if (check_kw("else")) {
            advance();
            if (check_kw("if")) {
                advance();
                // `else if constexpr (…)` may restate the modifier; inherit it regardless.
                const bool cx = eat_constexpr_modifier() || is_constexpr;
                StmtPtr nested = parse_if_rest(cx);  // else if …
                if (nested) n->else_body.push_back(std::move(nested));
            } else {
                n->else_body = parse_block();
            }
        }
        return n;
    }

    StmtPtr parse_match(bool is_constexpr = false) {
        advance();  // match
        auto m = std::make_unique<Match>();
        m->is_constexpr = is_constexpr;
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

    // with <resource> [as <name>] { … }  — bind a resource for the block's lifetime; its
    // RAII destructor runs when the block exits (see the With AST node).
    StmtPtr parse_with() {
        advance();  // with
        auto n = std::make_unique<With>();
        n->resource = parse_expr();
        if (!n->resource) {
            synchronize();
            return nullptr;
        }
        if (check_kw("as")) {
            advance();  // as
            if (!check(TokenKind::Identifier)) {
                error("expected a name after 'as' in a `with` statement");
                synchronize();
                return nullptr;
            }
            n->bind = advance().text;
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
        // One or more handlers, then an optional finally. A try with neither handles nothing and is
        // almost certainly a typo, so it is a diagnostic rather than a silently useless block.
        while (check_kw("except")) {
            advance();  // except
            Handler h;
            if (check(TokenKind::Identifier)) {
                h.var = advance().text;
            }
            // `of` and `finally` are CONTEXTUAL, matched as bare identifiers rather than added to the
            // keyword table — reserving them would take two ordinary words out of circulation, and `of`
            // in particular is a name programs use.
            if (check_ident("of")) {
                advance();  // of
                h.kind = parse_expr();
                if (!h.kind) {
                    error("expected a kind expression after 'of'");
                    synchronize();
                    return t;
                }
            }
            h.body = parse_block();
            t->handlers.push_back(std::move(h));
        }
        if (check_ident("finally")) {
            advance();
            t->finally_body = parse_block();
            t->has_finally = true;
        }
        if (t->handlers.empty() && !t->has_finally) {
            error("expected 'except' or 'finally' after a try block");
        }
        return t;
    }

    StmtPtr parse_raise() {
        advance();  // raise
        auto r = std::make_unique<Raise>();
        // A bare `raise` re-raises the error being handled. Same end-of-statement test `return` uses.
        if (check(TokenKind::Newline) || check(TokenKind::Semicolon) || check(TokenKind::RBrace) ||
            at_end()) {
            return r;
        }
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
        if (check(TokenKind::Assign) || check(TokenKind::PlusAssign) ||
            check(TokenKind::MinusAssign) || check(TokenKind::StarAssign) ||
            check(TokenKind::SlashAssign)) {
            auto a = std::make_unique<Assign>();
            a->op = advance().text;  // "=", "+=", "-=", "*=", "/="
            // A compound operator needs to READ the target before writing it, and a slice read
            // yields a fresh sequence — so `xs[a:b] += ys` would update a copy and throw it away.
            // Plain `=` is supported; this refuses rather than repeating that class of bug.
            if (a->op != "=" && e->kind == ExprKind::Slice) {
                error("cannot use `" + a->op + "` on a slice: write `xs[a:b] = xs[a:b] " +
                      a->op.substr(0, 1) + " ...` instead");
            }
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

    // type := IDENT ( '<' (type | NUMBER) (',' (type | NUMBER))* '>' )?
    // ONE angle-bracket spelling everywhere — struct fields, `let` annotations, and interface method
    // params all read like params/returns (parse_type_string) and explicit call template args:
    // `list<int>`, `dict<str, int>`, `array<float, 1024>`, `ndarray<float>`, nested `list<ndarray<float>>`
    // (the lexer emits `>>` as two Greater tokens, closed one level per recursion).
    TypeRef parse_type() {
        TypeRef t;
        if (!check(TokenKind::Identifier)) {
            error("expected a type name");
            return t;
        }
        t.name = advance().text;
        // A MODULE-QUALIFIED type (`fixarray.Fixed<f32, 3>`, `state.State`): capture the full
        // dotted + generic spelling into `qualified`, mapped later by the module-aware
        // map_type_string (the free map_type cannot resolve import aliases). Plain/container/width
        // types (no dot) fall through to the existing name/args parsing below, unchanged.
        if (check(TokenKind::Dot)) {
            t.qualified = t.name;
            while (check(TokenKind::Dot)) {
                advance();  // .
                if (!check(TokenKind::Identifier)) { error("expected a type name after '.'"); break; }
                t.qualified += "." + advance().text;
            }
            if (check(TokenKind::Less)) {
                t.qualified += advance().text;  // '<'
                for (;;) {
                    if (check(TokenKind::Number)) t.qualified += advance().text;       // non-type extent
                    else if (check(TokenKind::Identifier)) t.qualified += parse_type_string();  // (nested) type arg
                    else { error("expected a type or size inside '<...>'"); break; }
                    if (check(TokenKind::Comma)) { t.qualified += advance().text; continue; }
                    break;
                }
                if (check(TokenKind::Greater)) t.qualified += advance().text;
                else error("expected '>' to close the type arguments");
            }
            return t;
        }
        if (check(TokenKind::LBracket)) {
            // The OLD square-bracket type spelling. Give a clear, actionable message (type arguments
            // were unified onto angle brackets), then consume the [...] group so it does not cascade.
            error("type arguments use angle brackets now — write `" + t.name + "<...>` not `" +
                  t.name + "[...]` (e.g. `list<int>`, `dict<str, int>`, `array<float, 4>`, "
                          "`ndarray<float>`). Update this type annotation.");
            advance();  // [
            int depth = 1;
            while (!at_end() && depth > 0) {
                if (check(TokenKind::LBracket)) ++depth;
                else if (check(TokenKind::RBracket)) --depth;
                advance();
            }
            return t;
        }
        if (check(TokenKind::Less)) {
            advance();
            for (;;) {
                if (check(TokenKind::Number)) {
                    t.array_size = advance().text;  // array<T, N>
                } else if (check(TokenKind::Identifier)) {
                    t.args.push_back(parse_type());
                } else {
                    error("expected a type or size inside '<...>'");
                    break;
                }
                if (check(TokenKind::Comma)) {
                    advance();
                    continue;
                }
                break;
            }
            if (check(TokenKind::Greater)) {
                advance();
            } else {
                error("expected '>' to close the type");
            }
        }
        return t;
    }

    // A single type argument inside `<...>`: a name, optionally itself parameterized
    // (`ndarray<float>`). Distinct from parse_type()'s `[...]` form — explicit template
    // args mirror the C++/source spelling `Name<T>`.
    TypeRef parse_angle_type() {
        TypeRef t;
        t.name = advance().text;  // Identifier (the caller has already checked)
        if (check(TokenKind::Less)) {
            advance();
            for (;;) {
                if (!check(TokenKind::Identifier) && !check(TokenKind::Number)) break;
                t.args.push_back(parse_angle_arg());
                if (check(TokenKind::Comma)) { advance(); continue; }
                break;
            }
            if (check(TokenKind::Greater)) advance();
        }
        return t;
    }

    // One template argument inside `<...>`: a type (possibly itself parameterized) OR a non-type
    // VALUE — an integer literal spliced into the C++ template-argument list verbatim, e.g. the
    // fixed sizes in `Store<float, 1024, 2>`.
    TypeRef parse_angle_arg() {
        if (check(TokenKind::Number)) {
            TypeRef t;
            t.name = advance().text;  // the literal text, emitted as-is
            t.is_value = true;
            return t;
        }
        return parse_angle_type();
    }

    // Speculatively parse `< T (, T)* >` as explicit template arguments, committing ONLY
    // if the list closes and the next token is `(` (a call) — leaving the cursor on that
    // `(`. On any mismatch it restores both the cursor and the diagnostics and returns
    // false, so an ordinary `a < b` comparison is untouched.
    bool try_parse_type_args(std::vector<TypeRef>& out) {
        const std::size_t save_pos = pos_;
        const std::size_t save_diag = diags_.size();
        auto bail = [&] {
            pos_ = save_pos;
            diags_.resize(save_diag);
            out.clear();
            return false;
        };
        advance();  // '<'
        for (;;) {
            if (!check(TokenKind::Identifier) && !check(TokenKind::Number)) return bail();
            out.push_back(parse_angle_arg());
            if (check(TokenKind::Comma)) { advance(); continue; }
            break;
        }
        if (!check(TokenKind::Greater)) return bail();
        advance();  // '>'
        if (!check(TokenKind::LParen)) return bail();
        return true;
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
            if (check_kw("in")) op = "in";  // membership: `k in d` / `x in xs` / `sub in s`
            else if (check(TokenKind::EqualEqual)) op = "==";
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
            else if (check(TokenKind::Percent)) op = "%";
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
        // Unary `&` (address-of): emitted to C++ verbatim, so a .purr can take the address of a value
        // and hand a raw pointer straight to a C API (e.g. `vk_create(&info, &out)`).
        if (check(TokenKind::Ampersand)) {
            advance();
            ExprPtr o = parse_unary();
            return o ? std::make_unique<Unary>("&", std::move(o)) : nullptr;
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
                std::vector<std::string> arg_names;
                std::vector<ExprPtr> args = parse_args(arg_names);
                if (!check(TokenKind::RParen)) {
                    error("expected ')' after arguments");
                    return nullptr;
                }
                advance();
                auto call = std::make_unique<Call>(std::move(e), std::move(args));
                call->arg_names = std::move(arg_names);
                e = std::move(call);
            } else if (check(TokenKind::Less) &&
                       (e->kind == ExprKind::Ident || e->kind == ExprKind::Member)) {
                // `Name<T, ...>(args)` — explicit template arguments on a construction/call.
                // cheatah's `<` is otherwise the comparison operator, so this only COMMITS
                // when the angle list closes AND is immediately followed by `(` (the one
                // position a type-arg list is unambiguous); otherwise it backtracks fully and
                // the `<` falls through to parse_comparison as an operator.
                std::vector<TypeRef> targs;
                if (!try_parse_type_args(targs)) break;
                advance();  // '(' (try_parse_type_args left us positioned on it)
                std::vector<std::string> arg_names;
                std::vector<ExprPtr> args = parse_args(arg_names);
                if (!check(TokenKind::RParen)) {
                    error("expected ')' after arguments");
                    return nullptr;
                }
                advance();
                auto call = std::make_unique<Call>(std::move(e), std::move(args));
                call->arg_names = std::move(arg_names);
                call->type_args = std::move(targs);
                e = std::move(call);
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
                    // A second colon is a STEP (`a[::2]`, `a[1:9:2]`). Caught here so it reads as
                    // the unsupported feature it is, rather than as "expected an expression".
                    if (check(TokenKind::Colon)) {
                        error("step slices (a[::2]) are not supported; slice with a[lo:hi] and "
                              "step separately");
                        return nullptr;
                    }
                    if (!check(TokenKind::RBracket)) {
                        last = parse_expr();
                        if (!last) return nullptr;
                    }
                    if (check(TokenKind::Colon)) {
                        error("step slices (a[lo:hi:step]) are not supported; slice with a[lo:hi] "
                              "and step separately");
                        return nullptr;
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
                    // `obj[i, j, ...]` — multi-index subscript (an ndarray element).
                    std::vector<ExprPtr> extra;
                    while (check(TokenKind::Comma)) {
                        advance();
                        ExprPtr next = parse_expr();
                        if (!next) return nullptr;
                        extra.push_back(std::move(next));
                    }
                    if (!check(TokenKind::RBracket)) {
                        error("expected ']' after index");
                        return nullptr;
                    }
                    advance();
                    auto ix = std::make_unique<Index>(std::move(e), std::move(first));
                    ix->extra = std::move(extra);
                    e = std::move(ix);
                }
            } else {
                break;
            }
        }
        return e;
    }

    std::vector<ExprPtr> parse_args(std::vector<std::string>& names) {
        std::vector<ExprPtr> args;
        if (check(TokenKind::RParen)) return args;
        for (;;) {
            // `name = value` is a KEYWORD argument (the `=` lookahead keeps `==` an operator).
            std::string name;
            if (check(TokenKind::Identifier) && check_next(TokenKind::Assign)) {
                name = advance().text;
                advance();  // =
            } else if (!names.empty() && !names.back().empty()) {
                error("positional argument may not follow a keyword argument");
            }
            ExprPtr a = parse_expr();
            if (!a) break;
            args.push_back(std::move(a));
            names.push_back(std::move(name));
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

    std::span<const Token> toks_;
    std::size_t pos_ = 0;
    std::vector<Diagnostic> diags_;
};

// Attach the lexer's line-leading `#` comments to the AST as documentation. Adjacent
// comment lines form one block; a block whose last line sits DIRECTLY above a fn/
// struct/enum/interface declaration (or a struct method) becomes that node's `doc`.
// The file's first block, when it is not glued to a declaration, is the module doc
// (Program::module_doc). Blocks elsewhere — inside bodies, or separated from the next
// declaration by a blank line — are deliberately not documentation and stay dropped.
void attach_docs(Program& prog, const std::vector<CommentLine>& comments) {
    if (comments.empty()) return;

    std::map<unsigned, Stmt*> targets;         // declaration start line -> node
    std::map<unsigned, std::string*> fields;   // struct-field line -> &field.doc
    for (const StmtPtr& s : prog.body) {
        switch (s->kind) {
            case StmtKind::FnDef:
            case StmtKind::StructDef:
            case StmtKind::EnumDef:
            case StmtKind::InterfaceDef:
                if (s->line) targets[s->line] = s.get();
                if (s->kind == StmtKind::StructDef) {
                    auto& sd = static_cast<StructDef&>(*s);
                    for (const StmtPtr& m : sd.methods)
                        if (m->line) targets[m->line] = m.get();
                    for (Field& f : sd.fields)
                        if (f.line) fields[f.line] = &f.doc;  // fields carry their own Javadoc too
                }
                break;
            default:
                break;
        }
    }

    unsigned first_stmt_line = 0;  // the module doc must sit above the first statement
    for (const StmtPtr& s : prog.body) {
        if (s->line) { first_stmt_line = s->line; break; }
    }

    for (std::size_t i = 0; i < comments.size();) {
        std::size_t j = i;  // [i, j] = one contiguous block
        while (j + 1 < comments.size() &&
               comments[j + 1].pos.line == comments[j].pos.line + 1) {
            ++j;
        }
        std::string text;
        for (std::size_t k = i; k <= j; ++k) {
            std::string_view line = comments[k].text;
            if (!line.empty() && line.front() == ' ') line.remove_prefix(1);  // "# x" -> "x"
            if (k > i) text += '\n';
            text += line;
        }
        const unsigned decl_line = comments[j].pos.line + 1;
        const auto target = targets.find(decl_line);
        const auto field = fields.find(decl_line);
        if (target != targets.end()) {
            target->second->doc = std::move(text);
        } else if (field != fields.end()) {
            *field->second = std::move(text);
        } else if (i == 0 && prog.module_doc.empty() &&
                   (first_stmt_line == 0 || comments[i].pos.line < first_stmt_line)) {
            prog.module_doc = std::move(text);
        }
        i = j + 1;
    }
}

} // namespace

ParseResult parse(const std::vector<Token>& tokens) { return Parser(tokens).run(); }

ParseResult parse_source(std::string_view source) {
    const LexResult lex = tokenize(source);
    ParseResult r = parse(lex.tokens);
    r.diagnostics.insert(r.diagnostics.begin(), lex.diagnostics.begin(), lex.diagnostics.end());
    attach_docs(r.program, lex.comments);
    return r;
}

} // namespace cheatah
