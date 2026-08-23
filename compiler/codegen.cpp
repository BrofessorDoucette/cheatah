// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "codegen.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <string>
#include <string_view>

namespace cheatah {

namespace {

// Every cheatah fn/method param lowers to a C++20 abbreviated function template.
// We constrain each with the baseline `Value` concept so no generated template is
// unconstrained — keeps the C++ compiler's errors comprehensible and gives the
// future purrc diagnostics a concept-failure hook. (See constrain-all-templates.)
// The `builtins::` qualifier is prepended at use (it follows the module alias).
// Parameters are FORWARDING REFERENCES (`auto&&`), not values: an lvalue argument
// binds by reference — no copy for strings/structs/lists/dicts/ndarrays, and
// in-place mutation through the parameter is visible to the caller (Python's
// object semantics) — while a temporary still binds and lives for the call.
constexpr const char* kValueConcept = "Value auto&& ";

// Bare names that are Python built-ins (always available, no import) -> their C++
// symbol within the builtins module. Keyword-named conversions map specially. The
// caller prepends the (aliased) builtins namespace.
std::optional<std::string> builtin_cpp_name(const std::string& name) {
    static const std::map<std::string, std::string> kBuiltins = {
        {"len", "len"},   {"ord", "ord"},   {"chr", "chr"},     {"hex", "hex"},
        {"oct", "oct"},   {"bin", "bin"},   {"ascii", "ascii"}, {"hash", "hash"},
        {"bool", "to_bool"}, {"int", "to_int"}, {"float", "to_float"},
        {"str", "str"},
        {"append", "append"}, {"startswith", "startswith"},
        {"endswith", "endswith"}, {"contains", "contains"},
        // `Error(kind, message)` builds a raisable error; `raise "msg"` builds one implicitly.
        {"Error", "Error"},
    };
    const auto it = kBuiltins.find(name);
    if (it == kBuiltins.end()) return std::nullopt;
    return it->second;
}

// The C++ type behind a cheatah explicit-width name (the ONE canonical table). Every width has
// BOTH spellings — abbreviated (f32) and full (float32) — inserted from ONE entry per width, so
// the two spellings are the same type BY CONSTRUCTION, not by convention. The canonical types are
// what graphics/realtime code standardizes on: the <cstdint> EXACT-width integers (std::int8_t …
// std::uint64_t — core spellings like `int`/`short` only guarantee minimums), and IEEE-754
// binary32/64 as `float`/`double` (the community floats; C++23's std::float32_t is not yet
// portable enough to be the canon). builtins.hpp includes <cstdint>, so the spellings always
// resolve in generated code. This is the SINGLE SOURCE OF TRUTH consulted by BOTH the `sizeof`
// name table AND the declarable-type mappers (map_type / map_type_string / return_type_cpp), so
// a width usable in `sizeof(i32)` is exactly a width usable as `let x: i32` — they cannot drift.
// A width name is an OPT-IN storage type: cheatah's own `int` stays i64 and `float` stays f64
// (implementation policy), so a program only narrows where it explicitly asks for a width.
std::optional<std::string> width_cpp_type(const std::string& name) {
    static const std::map<std::string, std::string> kSpellings = [] {
        std::map<std::string, std::string> m;
        // {abbreviated, full, the <cstdint> name, the one canonical C++ type all three deduce to}.
        // Three spellings per integer width so a user names whichever they prefer with no
        // difference: our short `i32`, the long `int32`, OR the original C library name `int32_t`
        // (`std::int32_t` without the namespace) — all the SAME type by construction. The floats
        // have no <cstdint> name (that header is integer-only), so they carry just the two spellings.
        static constexpr struct { const char* abbrev; const char* full; const char* stdname; const char* cpp; } kWidths[] = {
            {"f32", "float32", nullptr,     "float"},          {"f64", "float64", nullptr,     "double"},
            {"i8",  "int8",    "int8_t",    "std::int8_t"},    {"i16", "int16",   "int16_t",   "std::int16_t"},
            {"i32", "int32",   "int32_t",   "std::int32_t"},   {"i64", "int64",   "int64_t",   "std::int64_t"},
            {"u8",  "uint8",   "uint8_t",   "std::uint8_t"},   {"u16", "uint16",  "uint16_t",  "std::uint16_t"},
            {"u32", "uint32",  "uint32_t",  "std::uint32_t"},  {"u64", "uint64",  "uint64_t",  "std::uint64_t"},
        };
        for (const auto& w : kWidths) {
            m.emplace(w.abbrev, w.cpp);
            m.emplace(w.full, w.cpp);
            if (w.stdname) m.emplace(w.stdname, w.cpp);  // the original cstdint spelling (int32_t, …)
        }
        return m;
    }();
    const auto it = kSpellings.find(name);
    if (it == kSpellings.end()) return std::nullopt;
    return it->second;
}

// The C++ spelling behind a cheatah `sizeof(<type>)` argument: the explicit widths above plus
// cheatah's own value types (whose widths are policy — int is i64, float f64 — which is exactly
// why foreign GPU/wire/file layouts should be sized with the width names instead).
std::optional<std::string> sizeof_type_spelling(const std::string& name) {
    if (auto w = width_cpp_type(name)) return w;
    if (name == "int") return "long long";
    if (name == "float") return "double";
    if (name == "bool") return "bool";
    return std::nullopt;
}

// C++ keywords a cheatah identifier can legally be but C++ cannot. cheatah's own keyword set
// (is_keyword, lexer.cpp) is far smaller, so `delete`, `new`, `default`, `class`, `int`, … are
// valid cheatah identifiers yet reserved in C++ — emitting one verbatim (`auto delete(...)`)
// fails to compile. Sorted for binary_search; keep alphabetical.
bool is_cpp_keyword(std::string_view w) {
    static constexpr std::array<std::string_view, 79> kCppKeywords{
        "alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch", "char",
        "char16_t", "char32_t", "char8_t", "class", "const", "const_cast", "consteval",
        "constexpr", "constinit", "continue", "decltype", "default", "delete", "do", "double",
        "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for",
        "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new",
        "noexcept", "nullptr", "operator", "private", "protected", "public", "register",
        "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static",
        "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
        "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
        "virtual", "void", "volatile", "wchar_t", "while",
    };
    return std::binary_search(kCppKeywords.begin(), kCppKeywords.end(), w);
}

// The C++-safe spelling of a cheatah IDENTIFIER (fn/method/param/variable/field/enum-member):
// append '_' to dodge a C++ keyword clash. Applied SYMMETRICALLY at declaration and every use
// site so decl and reference always match; string-literal contexts (JSON keys, print labels,
// enum debug names) keep the original spelling. Type names are NOT routed through this (naming
// a struct/enum/interface after a C++ keyword is unsupported).
std::string cpp_ident(std::string_view name) {
    return is_cpp_keyword(name) ? std::string(name) + "_" : std::string(name);
}

// Does an lvalue expression have `self` at its root (self / self.x / self.xs[i])?
bool is_self_rooted(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Ident: return static_cast<const Ident&>(e).name == "self";
        case ExprKind::Member: return is_self_rooted(*static_cast<const Member&>(e).object);
        case ExprKind::Index: return is_self_rooted(*static_cast<const Index&>(e).object);
        default: return false;
    }
}
bool block_mutates_self(const Block& body);  // fwd
// Whether a statement assigns through `self` (so the method must be non-const).
bool stmt_mutates_self(const Stmt& s) {
    switch (s.kind) {
        case StmtKind::Assign:
            return is_self_rooted(*static_cast<const Assign&>(s).target);
        case StmtKind::If: {
            const auto& n = static_cast<const If&>(s);
            return block_mutates_self(n.then_body) || block_mutates_self(n.else_body);
        }
        case StmtKind::While:
            return block_mutates_self(static_cast<const While&>(s).body);
        case StmtKind::For:
            return block_mutates_self(static_cast<const For&>(s).body);
        case StmtKind::With:
            return block_mutates_self(static_cast<const With&>(s).body);
        case StmtKind::Try: {
            const auto& t = static_cast<const Try&>(s);
            if (block_mutates_self(t.body)) return true;
            for (const Handler& h : t.handlers) {
                if (block_mutates_self(h.body)) return true;
            }
            return t.has_finally && block_mutates_self(t.finally_body);
        }
        case StmtKind::Match: {
            const auto& m = static_cast<const Match&>(s);
            return std::ranges::any_of(m.cases, [](const MatchCase& c) {
                return block_mutates_self(c.body);
            });
        }
        default:
            return false;
    }
}
bool block_mutates_self(const Block& body) {
    return std::ranges::any_of(body, [](const StmtPtr& s) { return stmt_mutates_self(*s); });
}

// Flatten a left-associated `+` chain (`((a + b) + c)`) into its operands in
// left-to-right order ([a, b, c]) — used to turn `x = x + a + b` into in-place
// appends and avoid full-string-copy temporaries.
void flatten_add(const Expr& e, std::vector<const Expr*>& out) {
    if (e.kind == ExprKind::Binary && static_cast<const Binary&>(e).op == "+") {
        const auto& b = static_cast<const Binary&>(e);
        flatten_add(*b.lhs, out);
        out.push_back(b.rhs.get());
    } else {
        out.push_back(&e);
    }
}
// Whether expression @p e references the variable @p name anywhere (so a self-append
// rewrite stays correct only when the appended operands don't read the target).
bool refers_to(const Expr& e, const std::string& name) {
    switch (e.kind) {
        case ExprKind::Ident:
            return static_cast<const Ident&>(e).name == name;
        case ExprKind::Member:
            return refers_to(*static_cast<const Member&>(e).object, name);
        case ExprKind::Index: {
            const auto& ix = static_cast<const Index&>(e);
            return refers_to(*ix.object, name) || refers_to(*ix.index, name);
        }
        case ExprKind::Slice: {
            const auto& sl = static_cast<const Slice&>(e);
            return refers_to(*sl.object, name) || (sl.start && refers_to(*sl.start, name)) ||
                   (sl.stop && refers_to(*sl.stop, name));
        }
        case ExprKind::Call: {
            const auto& c = static_cast<const Call&>(e);
            if (refers_to(*c.callee, name)) return true;
            return std::ranges::any_of(c.args, [&](const ExprPtr& a) { return refers_to(*a, name); });
        }
        case ExprKind::Unary:
            return refers_to(*static_cast<const Unary&>(e).operand, name);
        case ExprKind::Binary: {
            const auto& b = static_cast<const Binary&>(e);
            return refers_to(*b.lhs, name) || refers_to(*b.rhs, name);
        }
        case ExprKind::ListLit: {
            const auto refers = [&](const ExprPtr& el) { return refers_to(*el, name); };
            return std::ranges::any_of(static_cast<const ListLit&>(e).elements, refers);
        }
        case ExprKind::DictLit: {
            const auto& d = static_cast<const DictLit&>(e);
            const auto refers = [&](const ExprPtr& x) { return refers_to(*x, name); };
            return std::ranges::any_of(d.keys, refers) || std::ranges::any_of(d.values, refers);
        }
        case ExprKind::StructInit: {
            const auto refers = [&](const ExprPtr& v) { return refers_to(*v, name); };
            return std::ranges::any_of(static_cast<const StructInit&>(e).values, refers);
        }
        default:
            return false;
    }
}

// cheatah "value methods": `obj.f(a)` routes to `cheatah::builtins::f(obj, a)`.
// Kept to a known set so real member methods on module classes (e.g. io's File)
// are left as direct `obj.f(a)` calls.
bool is_builtin_method(const std::string& name) {
    return name == "append" || name == "startswith" || name == "endswith" ||
           name == "contains";
}

std::string cpp_string_literal(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    out += "\"";
    return out;
}

// cheatah type -> C++ type. Primitives, containers (STL), and struct names.
std::string map_type(const TypeRef& t) {
    if (t.name == "int") return "long long";
    if (t.name == "float") return "double";
    if (t.name == "str") return "std::string";
    if (t.name == "bool") return "bool";
    // An explicit-width storage type (opt-in): `i32` -> std::int32_t, `f32` -> float, etc.
    // Consulted before the containers so `list[i8]`/`dict[str,u16]`/`array[i32,N]` recurse into it.
    if (auto w = width_cpp_type(t.name)) return *w;
    if (t.name == "list" && t.args.size() == 1) {
        return "std::vector<" + map_type(t.args[0]) + ">";
    }
    if (t.name == "dict" && t.args.size() == 2) {
        return "std::unordered_map<" + map_type(t.args[0]) + ", " + map_type(t.args[1]) + ">";
    }
    if (t.name == "array" && t.args.size() == 1 && !t.array_size.empty()) {
        return "std::array<" + map_type(t.args[0]) + ", " + t.array_size + ">";
    }
    if (t.name == "ndarray") {  // the element type follows the enforced param/return spelling
        if (t.args.size() == 1)  // ndarray[int] -> <long long>, ndarray[float] -> <double>, ndarray[i16] -> <std::int16_t>
            return "::cheatah::ndarray::basic_ndarray<" + map_type(t.args[0]) + ">";
        return "::cheatah::ndarray::NDArray";  // bare `ndarray` — the element-erased default (double)
    }
    return t.name;  // a struct name
}

// A cheatah type used as an explicit template ARGUMENT (`Store<float, 1024>`). Same mapping as
// map_type, plus the bare `ndarray` element-erased alias (a packet trigger keeps its native
// NDArray, never an element-typed param). A NON-TYPE argument (an integer literal like `1024`)
// is emitted verbatim.
std::string map_type_arg(const TypeRef& t) {
    if (t.is_value) return t.name;  // non-type template argument — the literal, as-is
    if (t.name == "ndarray" && t.args.empty()) return "::cheatah::ndarray::NDArray";
    return map_type(t);
}

// Whether a type annotation involves an explicit-width name (i32/f32/u8/…) ANYWHERE — the
// element of a `list[i8]`, the value of a `dict[str,u8]`, and so on. Only such a declared type
// must OVERRIDE literal CTAD (which would deduce the default long long/double and mis-type the
// container); a plain int/float/str type deduces correctly and is left untouched, so no existing
// codegen churns. See the contextual-typing branch in gen_expr's ListLit/DictLit.
bool type_uses_width(const TypeRef& t) {
    if (width_cpp_type(t.name)) return true;
    return std::ranges::any_of(t.args, [](const TypeRef& a) { return type_uses_width(a); });
}

// The same test on a flat type SPELLING (a `-> list<i8>` return hint reaches codegen as a
// string): scan its identifier tokens for a width name. Bracket/comma/space all delimit, so
// `list<dict<str,u8>>` finds `u8` and `list<int>` finds none.
bool spelling_uses_width(const std::string& s) {
    std::string tok;
    auto flush = [&]() -> bool { const bool w = !tok.empty() && width_cpp_type(tok).has_value(); tok.clear(); return w; };
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) tok += c;
        else if (flush()) return true;
    }
    return flush();
}

class Codegen {
public:
    // When set, gen_stmt emits `#line N "<file>"` so the C++ backend's diagnostics map
    // back to the original .purr source (used by `purrc --check` for editor errors).
    void set_source_file(std::string f) {
        source_file_ = std::move(f);
        line_directives_ = !source_file_.empty();
    }

    // Off disables dead-local elimination (the --no-remove-variables flag), so every `let`
    // is emitted verbatim. On (the default), a `let` whose variable is never read/printed/
    // returned is dropped — keeping its initializer only if that may have side effects.
    void set_remove_unused(bool on) { remove_unused_ = on; }

    // Pass 1, shared by program + library modes: collect imports (modules to link),
    // type names (structs/interfaces/enums for construction + param constraints), and
    // decide which module namespaces get a short alias. Returns whether `builtins`
    // itself can be aliased (blocked only if the program defines `builtins` itself).
    bool analyze(const Program& prog) {
        for (const StmtPtr& s : prog.body) {
            if (s->kind == StmtKind::Import) {
                const auto& imp = static_cast<const Import&>(*s);
                if (!imp.symbols.empty()) {
                    // A from-import (`import Sym from a.b`): each Sym binds to the FULL path a::b::Sym,
                    // so `Sym.MEMBER` / `Sym({…})` / `x: Sym` work WITHOUT the module prefix. A `using`
                    // (emitted by emit_aliases) makes the bare name resolve as a type; the aliases_ entry
                    // makes `Sym.MEMBER` lower to a::b::Sym::MEMBER via the normal module-path machinery.
                    for (const std::string& sym : imp.symbols) {
                        std::vector<std::string> path = imp.module;
                        path.push_back(sym);
                        aliases_[sym] = path;
                        from_imports_.emplace_back(sym, path);
                    }
                    roots_.insert(imp.module.front());
                    continue;
                }
                const std::string key = imp.alias.empty() ? imp.module.front() : imp.alias;
                // A non-aliased `import a.b.c` binds only the HEAD `a` (Python semantics: the
                // call site still writes `a.b.c.fn` in full, and the tail is appended there).
                // Binding the whole path to `a` would double the tail — `a.b.fn` would resolve
                // to `a::b::b::fn`. An `as` alias binds the FULL path, since the alias name
                // stands in for all of it (`import a.b.c as z` makes `z` mean `a::b::c`).
                aliases_[key] = imp.alias.empty()
                                    ? std::vector<std::string>{imp.module.front()}
                                    : imp.module;
                roots_.insert(imp.module.front());
            } else if (s->kind == StmtKind::StructDef) {
                {
                    const auto& sd = static_cast<const StructDef&>(*s);
                    struct_names_.insert(sd.name);
                    struct_fields_[sd.name] = sd.fields;  // for designated-init validation + printing
                }
            } else if (s->kind == StmtKind::FnDef) {
                const auto& fd = static_cast<const FnDef&>(*s);
                fn_defs_[fd.name] = &fd;  // signature registry for keyword-argument calls
            } else if (s->kind == StmtKind::InterfaceDef) {
                interface_names_.insert(static_cast<const InterfaceDef&>(*s).name);
            } else if (s->kind == StmtKind::EnumDef) {
                enum_names_.insert(static_cast<const EnumDef&>(*s).name);
            }
        }

        // Decide each module's namespace alias. Because everything is emitted inside a
        // dedicated namespace (`cheatah_program` for programs, `cheatah::<m>` for a
        // library), an alias can never clash with a global C symbol (`::random`,
        // `::time`, `::socket`) — so EVERY module can be shortened. The only thing that
        // blocks an alias is the code using that exact name as one of its own identifiers
        // (e.g. a `struct os`); then we stay explicit (`::cheatah::<module>::…`).
        collect_defined(prog.body);
        for (const std::string& root : roots_) {
            if (!defined_names_.contains(root)) aliased_roots_.insert(root);
        }
        const bool alias_builtins = !defined_names_.contains("builtins");
        builtins_ns_ = alias_builtins ? "builtins::" : "::cheatah::builtins::";
        return alias_builtins;
    }

    // Emit the shared `#include` preamble (the cheatah prelude, the enum <ostream> when
    // needed, and one header per imported module). @p pragma_once adds `#pragma once`
    // for a library header.
    void emit_preamble(std::ostringstream& os, bool pragma_once) {
        os << kGeneratedMarker << "\n";
        if (pragma_once) os << "#pragma once\n";
        // The prelude consolidates the std headers the generated code leans on, the
        // always-available built-ins, and the PURR_EXPORT macro — so every generated
        // file carries one line here instead of a dozen repeated #includes. Each
        // imported module still gets its own header below, so modules stay separate.
        os << "#include \"cheatah.hpp\"\n";
        // Enums generate an operator<< (their debug text form) using std::ostream, so
        // pull it in even when the code does not `import io`.
        if (!enum_names_.empty()) os << "#include <ostream>\n";
        for (const std::string& root : roots_) {
            os << "#include \"" << root << ".hpp\"\n";
        }
        os << "\n";
    }

    // Emit one short, DISTINCT namespace alias per module (io::, ndarray::, …) so the
    // body never repeats `cheatah::<module>::`. Valid inside any enclosing namespace.
    void emit_aliases(std::ostringstream& os, bool alias_builtins) {
        if (alias_builtins) os << "namespace builtins = ::cheatah::builtins;\n";
        for (const std::string& root : roots_) {
            if (aliased_roots_.contains(root))
                os << "namespace " << root << " = ::cheatah::" << root << ";\n";
        }
        // From-imports: a using-DECLARATION `using ::cheatah::<module>::Sym;` brings the bare symbol
        // into scope — a type (struct/enum) OR a function/value, unlike a type-alias which is
        // types-only. This matches the aliases_ entry that resolves `Sym.MEMBER` / `Sym(...)`.
        for (const auto& [sym, path] : from_imports_) {
            (void)sym;  // the name is the last path segment; the using-declaration re-introduces it
            os << "using ::cheatah";
            for (const std::string& seg : path) os << "::" << cpp_ident(seg);
            os << ";\n";
        }
        os << "\n";
    }

    // Emit the type definitions (enums, interfaces/concepts, structs) shared by both
    // modes. Functions are emitted separately (program vs library differ there).
    void emit_types(std::ostringstream& os, const Program& prog) {
        for (const StmtPtr& s : prog.body)
            if (s->kind == StmtKind::EnumDef) gen_enum(os, static_cast<const EnumDef&>(*s));
        for (const StmtPtr& s : prog.body)
            if (s->kind == StmtKind::InterfaceDef)
                gen_interface(os, static_cast<const InterfaceDef&>(*s));
        for (const StmtPtr& s : prog.body)
            if (s->kind == StmtKind::StructDef) gen_struct(os, static_cast<const StructDef&>(*s));
    }

    // The TYPED JSON READER bridge: when the program imports `parsers`, synthesize a
    // `cheatah::parsers::json::schema<>` specialization for every struct it defines, so
    // `parsers.json.read(text, value)` parses JSON straight into the user's own structs —
    // the compiler knows each struct's field names and types, so the user writes NO schema.
    // Specializations must live in the parsers namespace, so the enclosing program/library
    // namespace (@p ns) is closed around them and reopened after.
    void emit_json_schemas(std::ostringstream& os, const Program& prog, const std::string& ns) {
        if (!roots_.contains("parsers")) return;  // reader not imported: nothing to synthesize
        bool any = false;
        for (const StmtPtr& s : prog.body) {
            if (s->kind != StmtKind::StructDef) continue;
            const auto& sd = static_cast<const StructDef&>(*s);
            if (sd.fields.empty()) continue;  // nothing to read into
            if (!any) {
                os << "}  // namespace " << ns << " (paused for JSON schema synthesis)\n"
                   << "namespace cheatah::parsers::json {\n";
                any = true;
            }
            if (emit_docs_)
                os << "/** JSON schema for `" << sd.name << "`, synthesized by purrc from the "
                      "struct's fields — powers `parsers.json.read` into this type. */\n";
            os << "template <> inline constexpr auto schema<::" << ns << "::" << sd.name
               << "> = object(";
            for (std::size_t i = 0; i < sd.fields.size(); ++i) {
                if (i) os << ", ";
                os << "field(\"" << sd.fields[i].name << "\", &::" << ns << "::" << sd.name
                   << "::" << cpp_ident(sd.fields[i].name) << ")";
            }
            os << ");\n";
        }
        if (any) {
            os << "}  // namespace cheatah::parsers::json\n"
               << "namespace " << ns << " {\n\n";
        }
    }

    // Register enums declared in a folded C++ base (see --base-header) so the .purr can name their
    // members: a base `enum class Source {…}` makes `Source.PRIOR` lower to `Source::PRIOR`, exactly as
    // a .purr-declared enum does. Without this, a module cannot move its enums into its sibling header
    // and still reference them from its own .purr. Scanned as text — purrc has no C++ front end.
    void scan_base_enums(const std::string& base) {
        auto word = [](char c) { return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_'; };
        std::size_t p = 0;
        while ((p = base.find("enum", p)) != std::string::npos) {
            const bool left_ok = (p == 0) || !word(base[p - 1]);
            std::size_t q = p + 4;
            if (!left_ok || q >= base.size() || !std::isspace(static_cast<unsigned char>(base[q]))) {
                p += 4;
                continue;
            }
            auto skip_ws = [&] { while (q < base.size() && std::isspace(static_cast<unsigned char>(base[q]))) ++q; };
            auto skip_kw = [&](const char* kw) {
                const std::size_t n = std::char_traits<char>::length(kw);
                if (base.compare(q, n, kw) == 0 && q + n < base.size() &&
                    std::isspace(static_cast<unsigned char>(base[q + n]))) { q += n; skip_ws(); }
            };
            skip_ws();
            skip_kw("class");
            skip_kw("struct");
            const std::size_t s = q;
            while (q < base.size() && word(base[q])) ++q;
            const std::size_t name_end = q;
            skip_ws();
            // A real declaration is followed by `{` (body), `:` (fixed underlying type) or `;` (forward
            // decl) — this filters the word "enum" appearing in a comment or a string.
            if (name_end > s && q < base.size() && (base[q] == '{' || base[q] == ':' || base[q] == ';'))
                enum_names_.insert(base.substr(s, name_end - s));
            p = name_end;
        }
    }

    CodegenResult run(const Program& prog, const std::string& base_hoist = "",
                      const std::string& base_body = "") {
        const bool alias_builtins = analyze(prog);
        scan_base_enums(base_body);

        std::ostringstream os;
        emit_preamble(os, /*pragma_once=*/false);

        // A folded sibling C++ base: its #include/#pragma lines at file scope after the preamble.
        if (!base_hoist.empty()) os << base_hoist << "\n";

        // Raw C++ escape hatch: top-level `cpp { … }` blocks stay at FILE SCOPE (outside
        // the program namespace below), so they can carry #includes and global helpers
        // the rest of the program can use.
        for (const StmtPtr& s : prog.body) {
            if (s->kind == StmtKind::RawCpp) {
                os << static_cast<const RawCpp&>(*s).code << "\n";
            }
        }
        // The folded base's body sits beside the cpp{} helpers, at file scope, so the program can
        // call it by unqualified lookup from inside cheatah_program.
        if (!base_body.empty()) os << base_body << "\n";

        // The whole program lives in a dedicated namespace. This is what makes the module
        // aliases SAFE: a `namespace random = ::cheatah::random;` here cannot redefine the
        // global C `::random` (different scope), so every module name — even ones that
        // match a libc function — shortens cleanly with no global pollution. The exported
        // entry point is a tiny extern-"C" trampoline at file scope (below).
        os << "namespace cheatah_program {\n\n";
        emit_aliases(os, alias_builtins);
        emit_types(os, prog);
        emit_json_schemas(os, prog, "cheatah_program");
        // Program functions live in an anonymous namespace: internal linkage like `static`, in
        // the form the C++ Core Guidelines ask for (misc-use-anonymous-namespace).
        const bool has_fns = std::ranges::any_of(prog.body, [](const StmtPtr& s) { return s->kind == StmtKind::FnDef; });
        if (has_fns) os << "namespace {\n\n";
        for (const StmtPtr& s : prog.body) {
            if (s->kind == StmtKind::FnDef) gen_fn(os, static_cast<const FnDef&>(*s));
        }
        if (has_fns) os << "}  // namespace\n\n";

        // The program body, as an internal function the exported trampoline calls. Its
        // name must not collide with a program function the user defined (e.g. a
        // `fn run`/`fn purr_main`), so pick the first `purr_main`/`purr_main_`/… free of
        // defined_names_.
        std::string entry = "purr_main";
        while (defined_names_.contains(entry)) entry += "_";
        os << "void " << entry << "() {\n";
        deferred_lets_.clear(); const_vars_.clear();  // no-value lets in the top-level body are scoped to it
        for (std::size_t i = 0; i < prog.body.size(); ++i) {
            const Stmt& s = *prog.body[i];
            if (s.kind == StmtKind::Import || s.kind == StmtKind::StructDef ||
                s.kind == StmtKind::FnDef || s.kind == StmtKind::RawCpp ||
                s.kind == StmtKind::InterfaceDef || s.kind == StmtKind::EnumDef) {
                continue;  // emitted above (imports -> includes; top-level cpp -> file scope)
            }
            if (remove_unused_ && s.kind == StmtKind::Let &&
                try_drop_dead_let(os, s, prog.body, i + 1, "    ")) {
                continue;  // unused top-level `let` -> minimal form (side effects preserved)
            }
            gen_stmt(os, s, "    ");
        }
        os << "}\n";
        os << "\n}  // namespace cheatah_program\n\n";

        // The runtime resolves this C symbol and calls it; it just enters the program.
        os << "PURR_EXPORT void purr_main() { cheatah_program::" << entry << "(); }\n";

        CodegenResult r;
        r.source = os.str();
        for (const std::string& root : roots_) r.modules.push_back(root);
        r.diagnostics = std::move(diags_);
        return r;
    }

    // Library mode: emit @p prog as an importable cheatah library module in
    // `namespace cheatah::<opts.module_name>`. No `purr_main`. See codegen_library().
    CodegenResult run_library(const Program& prog, const LibOptions& opts) {
        emit_docs_ = true;  // a library header is the module's documented public surface
        const bool alias_builtins = analyze(prog);
        scan_base_enums(opts.base_body);

        // Header: the importable surface. `#pragma once` because consumers #include it.
        std::ostringstream hdr;
        emit_preamble(hdr, /*pragma_once=*/true);

        // A folded sibling C++ base: its #include/#pragma lines at file scope, after the module
        // #includes and before the namespace (an #include cannot appear inside a namespace).
        if (!opts.base_hoist.empty()) hdr << opts.base_hoist << "\n";
        // The .purr module doc block becomes the header's @file Javadoc, so Doxygen (and
        // the editor's hover DB built from its XML) documents a generated module exactly
        // like a hand-written one. JAVADOC_AUTOBRIEF makes the first sentence the brief.
        if (!prog.module_doc.empty()) {
            std::ostringstream file_doc;
            file_doc << "@file " << opts.module_name << ".hpp\n\n" << prog.module_doc;
            emit_doc(hdr, file_doc.str());
        }

        // Raw C++ escape hatch: top-level `cpp { … }` blocks stay at FILE SCOPE (outside the module
        // namespace below), so they can carry #includes and global helpers the module's functions use.
        // Mirrors the program-mode loop in run(); without it a top-level `cpp{}` in a library module is
        // silently dropped.
        for (const StmtPtr& s : prog.body) {
            if (s->kind == StmtKind::RawCpp) {
                hdr << static_cast<const RawCpp&>(*s).code << "\n";
            }
        }

        hdr << "namespace cheatah::" << opts.module_name << " {\n\n";
        emit_aliases(hdr, alias_builtins);
        emit_types(hdr, prog);
        emit_json_schemas(hdr, prog, "cheatah::" + opts.module_name);

        // The folded base's body: inside the module namespace, AFTER the transpiled types and BEFORE
        // the functions. So a hand-written C++ getter/setter CAN read a .purr struct's fields, and the
        // .purr functions CAN call the base (unqualified C++ lookup) — the header holds the accessor
        // layer, the .purr holds the heavy logic. This is the whole feature in one line.
        if (!opts.base_body.empty()) hdr << opts.base_body << "\n\n";

        // Functions. Today every cheatah function lowers to a constrained template
        // (untyped params -> `Value auto`), and templates MUST be header-visible in both
        // modes — so they are emitted inline here regardless of transparency. (Concrete,
        // non-template exports — header declaration + archive-hidden definition in opaque
        // builds — await an FnDef return-type annotation; see codegen_library() docs.)
        for (const StmtPtr& s : prog.body) {
            if (s->kind == StmtKind::FnDef) gen_fn_library(hdr, static_cast<const FnDef&>(*s));
        }

        // The module ABI/identity marker — auto-emitted into every cheatah library. It is
        // the one CONCRETELY-typed symbol, so in opaque builds it anchors a non-empty,
        // signed static archive; in transparent builds it is a plain inline accessor. A
        // consumer (or the runtime) can call it to confirm which module a header/archive
        // belongs to.
        std::ostringstream impl;  // opaque-only: definitions compiled into the archive
        hdr << "/// ABI/identity marker for the `" << opts.module_name
            << "` cheatah module: returns the module name.\n"
            << "///\n"
            << "/// Auto-emitted by purrc's library emitter. It is the concrete symbol that\n"
            << "/// anchors the module's signed static archive in opaque (source-hidden) builds.\n"
            << "/// @return the module name (`\"" << opts.module_name << "\"`).\n";
        if (opts.transparent) {
            hdr << "inline const char* module_abi() noexcept { return \"" << opts.module_name
                << "\"; }\n";
        } else {
            hdr << "const char* module_abi() noexcept;\n";
            impl << "// Generated by purrc — do not edit. Out-of-line definitions for the "
                 << opts.module_name << " module\n"
                 << "// (the header carries declarations + templates). Compiled into a signed archive\n"
                 << "// in an opaque build, or as a plain translation unit by the host build under --split.\n"
                 << "#include \"" << opts.module_name << ".hpp\"\n\n"
                 << "const char* cheatah::" << opts.module_name
                 << "::module_abi() noexcept { return \"" << opts.module_name << "\"; }\n";
        }

        hdr << "\n}  // namespace cheatah::" << opts.module_name << "\n";

        CodegenResult r;
        r.header_source = hdr.str();
        if (!opts.transparent) r.impl_source = impl.str();
        for (const std::string& root : roots_) r.modules.push_back(root);
        r.diagnostics = std::move(diags_);
        return r;
    }

private:
    // Re-emit a declaration's .purr doc comment as a Javadoc block. Library headers only
    // (emit_docs_): they are the module's public surface, read by Doxygen and the editor's
    // hover DB, and must carry the same documentation convention as the hand-written
    // stdlib headers. The block is verbatim — @param/@return/@complexity/@alloc/@test
    // tags written in the .purr comment pass straight through to Doxygen.
    void emit_doc(std::ostringstream& os, const std::string& doc, const char* indent = "") const {
        if (!emit_docs_ || doc.empty()) return;
        os << indent << "/**\n";
        for (std::size_t start = 0; start <= doc.size();) {
            const std::size_t end = doc.find('\n', start);
            // cppcheck-suppress stlcstrConstructor  // (ptr,len) subview of doc — not a c_str() copy
            const std::string_view line(doc.data() + start,
                                        (end == std::string::npos ? doc.size() : end) - start);
            os << indent << " *";
            if (!line.empty()) os << ' ' << line;
            os << "\n";
            if (end == std::string::npos) break;
            start = end + 1;
        }
        os << indent << " */\n";
    }

    // Library-mode free function: like gen_fn but WITHOUT `static` internal linkage, so the
    // function is usable across translation units that include the module header. A fn whose
    // params all lower to concrete types is NOT a template, so it needs `inline` explicitly or
    // a second including TU is an ODR violation (multiple definition at link); templates and
    // constexpr fns are implicitly inline, and the keyword is harmless on them, so every
    // non-constexpr library fn gets it. Same constrained-template lowering as gen_fn.
    void gen_fn_library(std::ostringstream& os, const FnDef& fd) {
        emit_doc(os, fd.doc);
        // Honour a `-> Type` hint exactly as program-mode gen_fn does. Without this the return type
        // is always deduced, so a body that only `raise`s deduces `void` and callers cannot use the
        // result — which makes an interface OUTLINE (declared surface, unimplemented bodies)
        // impossible to express in a library module.
        os << (fd.is_constexpr ? "constexpr " : "inline ") << return_type_cpp(fd.return_type) << " "
           << cpp_ident(fd.name) << "(";
        for (std::size_t i = 0; i < fd.params.size(); ++i) {
            os << (i != 0 ? ", " : "") << param_prefix(method_param_type(fd, i)) << cpp_ident(fd.params[i]);
        }
        os << ") {\n";
        deferred_lets_.clear(); const_vars_.clear();  // no-value lets are scoped to this function body
        return_type_hint_ = spelling_uses_width(fd.return_type) ? return_type_cpp(fd.return_type) : "";
        gen_block(os, fd.body, "    ");
        return_type_hint_.clear();
        os << "}\n\n";

        // Default parameters lower to FORWARDING OVERLOADS, one per trailing-default suffix —
        // a C++ default argument on an `auto` parameter cannot drive deduction, an overload can.
        for (std::size_t cut = fd.params.size(); cut-- > 0;) {
            if (cut >= fd.param_defaults.size() || !fd.param_defaults[cut]) {
                break;  // defaults are trailing; the first non-default ends the ladder
            }
            // Each synthesized overload is public API, so in library mode it carries its own
            // Javadoc (the gate requires 100% docs): a brief pointing at the primary, @param for
            // the args it keeps, @return when the primary documents one. (emit_doc is a no-op in
            // program mode, so a program's .gen.cpp stays lean.)
            std::string ov_doc = "Convenience overload of `" + fd.name +
                                 "` with the trailing default argument(s) applied.";
            for (std::size_t i = 0; i < cut; ++i)
                ov_doc += "\n@param " + fd.params[i] + " as documented on the primary overload.";
            if (fd.doc.find("@return") != std::string::npos)
                ov_doc += "\n@return as the primary overload.";
            emit_doc(os, ov_doc);
            os << "static " << (fd.is_constexpr ? "constexpr " : "") << return_type_cpp(fd.return_type)
           << " " << cpp_ident(fd.name) << "(";
            for (std::size_t i = 0; i < cut; ++i) {
                os << (i != 0 ? ", " : "") << param_prefix(method_param_type(fd, i)) << cpp_ident(fd.params[i]);
            }
            os << ") { return " << cpp_ident(fd.name) << "(";
            for (std::size_t i = 0; i < cut; ++i) {
                os << (i != 0 ? ", " : "") << cpp_ident(fd.params[i]);
            }
            for (std::size_t i = cut; i < fd.params.size(); ++i) {
                os << (i != 0 ? ", " : "") << gen_expr(*fd.param_defaults[i]);
            }
            os << "); }\n";
        }
        os << "\n";
    }

    // A MODULE-QUALIFIED type spelling (`state.State`, `memory.Owner<int>`) -> its C++ type,
    // resolving the leading import alias the SAME way a `state.State()` call does
    // (module_namespace). A name with no '.' is returned unchanged. Template arguments are mapped
    // through map_type_string, so a builtin spelled inside the angle brackets reaches C++ as its
    // real type: `memory.Owner<int>` -> `::cheatah::memory::Owner<long long>` (matching what
    // `memory.own(0)` deduces).
    std::string map_qualified_type(const std::string& type_name) const {
        const std::size_t lt = type_name.find('<');
        if (lt != std::string::npos) {
            std::string inner = type_name.substr(lt + 1);
            if (!inner.empty() && inner.back() == '>') inner.pop_back();
            return map_qualified_type(type_name.substr(0, lt)) + "<" + map_type_args(inner) + ">";
        }
        if (type_name.find('.') == std::string::npos) return type_name;
        std::vector<std::string> segs;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= type_name.size(); ++i) {
            if (i == type_name.size() || type_name[i] == '.') {
                segs.push_back(type_name.substr(start, i - start));
                start = i + 1;
            }
        }
        const auto it = aliases_.find(segs[0]);
        std::string out = (it != aliases_.end()) ? module_namespace(it->second)
                                                  : ("::cheatah::" + segs[0]);
        for (std::size_t i = 1; i < segs.size(); ++i) out += "::" + segs[i];
        return out;
    }

    // A comma-separated template-argument LIST from a type spelling, each argument mapped to its
    // C++ type. Splits at depth-0 commas only — a nested generic (`list<dict<str,int>>`) stays one
    // argument and recurses through map_type_string.
    std::string map_type_args(const std::string& args) const {
        std::string out;
        int depth = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= args.size(); ++i) {
            if (i == args.size() || (args[i] == ',' && depth == 0)) {
                if (!out.empty()) out += ", ";
                out += map_type_string(args.substr(start, i - start));
                start = i + 1;
            } else if (args[i] == '<') {
                ++depth;
            } else if (args[i] == '>') {
                --depth;
            }
        }
        return out;
    }

    // One cheatah type SPELLING (a parse_type_string result) -> its C++ type: builtins map to
    // their C++ types, a generic maps base + arguments, an integer literal (a non-type template
    // argument) and a plain struct/enum name stay verbatim. The string-level twin of map_type /
    // map_type_arg — annotations reach codegen as flat strings, so template arguments are
    // re-mapped here.
    std::string map_type_string(const std::string& spelling) const {
        if (spelling.empty()) return spelling;
        if (spelling.find_first_not_of("0123456789") == std::string::npos) {
            return spelling;  // a non-type template argument (`Store<float, 1024>`) — verbatim
        }
        const std::size_t lt = spelling.find('<');
        if (lt == std::string::npos) {
            if (spelling == "int") return "long long";
            if (spelling == "float") return "double";
            if (spelling == "str") return "std::string";
            if (spelling == "bool") return "bool";
            if (auto w = width_cpp_type(spelling)) return *w;  // opt-in width: i32 -> std::int32_t, etc.
            if (spelling == "ndarray") return "::cheatah::ndarray::NDArray";
            return map_qualified_type(spelling);  // dotted -> resolved; a plain name -> unchanged
        }
        const std::string base = spelling.substr(0, lt);
        std::string args = spelling.substr(lt + 1);
        if (!args.empty() && args.back() == '>') args.pop_back();
        if (base == "ndarray")  // ndarray[int]-><long long>, ndarray[float]-><double>, ndarray[i16]-><std::int16_t>
            return "::cheatah::ndarray::basic_ndarray<" + map_type_string(args) + ">";
        if (base == "list") return "std::vector<" + map_type_args(args) + ">";
        if (base == "dict") return "std::unordered_map<" + map_type_args(args) + ">";
        if (base == "array") return "std::array<" + map_type_args(args) + ">";
        return map_qualified_type(base) + "<" + map_type_args(args) + ">";
    }

    // A DECLARED type (a `let`/field annotation, parse_type output) -> its C++ type. A module-
    // qualified type (`fixarray.Fixed<f32, 3>`, `state.State`) carries its full spelling in
    // `qualified` and is mapped by the module-aware map_type_string (which resolves the import
    // alias); everything else goes through the free map_type. This is the ONE render seam for
    // parse_type results, so dotted types resolve wherever a declaration is emitted.
    std::string map_declared_type(const TypeRef& t) const {
        return t.qualified.empty() ? map_type(t) : map_type_string(t.qualified);
    }

    // The constraint prefix for a parameter declared with type @p type_name: an
    // interface name -> that concept (`Shape auto&&`); otherwise the baseline Value.
    // Both are forwarding references — see kValueConcept.
    std::string param_prefix(const std::string& type_name) const {
        // A `const`-qualified param (encoded "const T" by the parser) -> a const reference: map the
        // underlying type to its concrete reference and prepend const (so it binds a `const Array&`).
        if (type_name.starts_with("const ")) {
            return "const " + param_prefix(type_name.substr(6));
        }
        if (!type_name.empty() && interface_names_.contains(type_name)) {
            return type_name + " auto&& ";
        }
        // A module-qualified struct/class (`state.State`) -> a concrete mutable reference (so a bot's
        // callback mutates the caller's State in place, and the signature stays CTAD-readable).
        if (type_name.find('.') != std::string::npos) {
            return map_qualified_type(type_name) + "& ";
        }
        // `: ndarray<T>` (or a list of them) — the enforced spelling for an ndarray parameter:
        // a concrete mutable reference (in-place updates reach the caller's array; a wrong element
        // type fails to compile). Any element width flows through map_type_string, so `ndarray<i16>`
        // and `list<ndarray<u8>>` bind correctly alongside the int/float defaults.
        if (type_name.starts_with("ndarray<") || type_name.starts_with("list<ndarray<"))
            return map_type_string(type_name) + "& ";
        return builtins_ns_ + kValueConcept;
    }

    // The C++ RETURN type for a `-> Type` hint (a value, not a reference). Empty -> "auto"
    // (the abbreviated-template default, kept when no hint is given).
    std::string return_type_cpp(const std::string& type_name) const {
        if (type_name.empty()) return "auto";
        // ndarray[T] value return (any element width) — delegate to the shared spelling mapper.
        if (type_name.starts_with("ndarray<")) return map_type_string(type_name);
        // `list<T>` — recurse on the element so `list<Material>` / `list<int>` / `list<i8>` map correctly.
        if (type_name.starts_with("list<") && type_name.back() == '>')
            return "std::vector<" + return_type_cpp(type_name.substr(5, type_name.size() - 6)) + ">";
        if (type_name == "int") return "long long";
        if (type_name == "float") return "double";
        if (type_name == "str") return "std::string";
        if (type_name == "bool") return "bool";
        if (auto w = width_cpp_type(type_name)) return *w;  // opt-in width return type
        if (type_name.find('.') != std::string::npos) return map_qualified_type(type_name);
        return type_name;  // a struct / enum / void name (emitted verbatim)
    }

    // The C++ type for a typed PARAMETER in a concrete forwarding lambda (ndarrays by
    // mutable reference so in-place updates reach the caller; scalars/structs by value).
    std::string lambda_param_cpp(const std::string& type_name) const {
        if (type_name.starts_with("const ")) {
            return "const " + lambda_param_cpp(type_name.substr(6));
        }
        // `ndarray<T>` / `list<ndarray<T>>` by mutable reference (any element width via map_type_string).
        if (type_name.starts_with("ndarray<") || type_name.starts_with("list<ndarray<"))
            return map_type_string(type_name) + "&";
        // A module-qualified struct/class (`state.State`) binds by reference (the brain's callbacks
        // mutate the caller's State/Memory in place; matches the param_prefix spelling for CTAD).
        if (type_name.find('.') != std::string::npos) return map_qualified_type(type_name) + "&";
        return return_type_cpp(type_name);
    }

    // A fn is FULLY TYPED when it has a return-type hint and every parameter has a type hint.
    // Such a fn can lower to a concrete-signature callback (see gen_args), so its signature is
    // readable by CTAD on the receiving side. Untyped fns keep the generic forwarding lambda.
    static bool fn_fully_typed(const FnDef& fd) {
        if (fd.return_type.empty() || fd.params.empty()) return false;
        for (std::size_t i = 0; i < fd.params.size(); ++i) {
            if (i >= fd.param_types.size() || fd.param_types[i].empty()) return false;
        }
        return true;
    }

    // interface -> a C++20 concept: every method must be callable on the type, so a
    // `struct S : Iface` whose methods don't match fails the static_assert below.
    // enum Name { A [= v], … } -> a scoped, type-safe `enum class`. The underlying
    // type is left implicit (int), as for a plain C++ `enum class`. An explicit value
    // is emitted as a C++ constant expression (so `A = 1`, or `B = A + 1` referring to
    // an earlier member, both work). Members are reached scoped (Name::A) — see the
    // Member case in gen_expr.
    void gen_enum(std::ostringstream& os, const EnumDef& ed) {
        emit_doc(os, ed.doc);
        // Implicitly-valued enumerators are 0..n-1, so the underlying type is sized to n: a
        // byte for up to 256 members (performance-enum-size; smaller structs). An explicit
        // value is an arbitrary constant expression we do not evaluate here, so those keep
        // the C++ default (int).
        const bool implicit_values = std::ranges::none_of(ed.enumerators, [](const Enumerator& en) { return static_cast<bool>(en.value); });
        os << "enum class " << ed.name;
        if (implicit_values && ed.enumerators.size() <= 256) os << " : std::uint8_t";
        os << " {\n";
        for (const Enumerator& en : ed.enumerators) {
            os << "    " << cpp_ident(en.name);
            if (en.value) os << " = " << gen_expr(*en.value);
            os << ",\n";  // a trailing comma is valid in a C++ enumerator list
        }
        os << "};\n";

        // A streamable text form for debugging — `io.print(c)` shows `Name.MEMBER`
        // (Python's `Color.RED` style). An if-chain (not a switch) so two members that
        // share a value — aliases — don't produce duplicate `case` labels; an unknown
        // value (e.g. from a cpp{} cast) prints `Name(<int>)`. This makes the enum
        // `Streamable`, so print/format/str-of-containers all pick it up via ADL.
        os << "inline std::ostream& operator<<(std::ostream& os_, " << ed.name << " v_) {\n";
        for (const Enumerator& en : ed.enumerators) {
            os << "    if (v_ == " << ed.name << "::" << cpp_ident(en.name) << ") return os_ << \""
               << ed.name << "." << en.name << "\";\n";
        }
        os << "    return os_ << \"" << ed.name
           << "(\" << static_cast<long long>(v_) << \")\";\n";
        os << "}\n\n";
    }

    void gen_interface(std::ostringstream& os, const InterfaceDef& id) {
        emit_doc(os, id.doc);
        os << "template <typename Self>\n";
        os << "concept " << id.name << " =";
        if (id.methods.empty()) {
            os << " true;\n\n";
            return;
        }
        for (std::size_t i = 0; i < id.methods.size(); ++i) {
            const InterfaceMethod& m = id.methods[i];
            os << (i == 0 ? "\n    " : " &&\n    ");
            os << "requires(Self& self) { self." << m.name << "(";
            for (std::size_t j = 0; j < m.param_types.size(); ++j) {
                const std::string ty =
                    m.param_types[j].name.empty() ? "long long" : map_type(m.param_types[j]);
                os << (j != 0 ? ", " : "") << "std::declval<" << ty << ">()";
            }
            os << "); }";
        }
        os << ";\n\n";
    }

    // Whether a field type is a byte-width integer (i8/u8 == signed/unsigned char). A struct's
    // generated pretty-print / operator<< stream fields DIRECTLY (not through str()), so these must
    // be promoted with unary `+` to print as NUMBERS, not characters — the same gotcha str() fixes.
    static bool is_char_width(const std::string& tname) {
        const auto w = width_cpp_type(tname);
        return w && (*w == "std::int8_t" || *w == "std::uint8_t");
    }

    void gen_struct(std::ostringstream& os, const StructDef& sd) {
        const bool streamable = struct_is_streamable(sd);
        emit_doc(os, sd.doc);
        os << "struct " << sd.name << " {\n";
        for (const Field& f : sd.fields) {
            emit_doc(os, f.doc, "    ");  // library mode: the field's `#` doc -> its Javadoc
            // A scalar field gets `{}`: a default-constructed record is zeroed rather than
            // indeterminate (pro-type-member-init); designated-init construction is unaffected.
            // Class-typed fields (string, containers, records) already default-construct, so an
            // initializer there would be redundant (readability-redundant-member-init).
            const std::string ftype = map_declared_type(f.type);
            const bool scalar = ftype == "long long" || ftype == "double" || ftype == "bool" || ftype == "float" ||
                                ftype.starts_with("std::int") || ftype.starts_with("std::uint");
            os << "    " << ftype << " " << cpp_ident(f.name) << (scalar ? "{};\n" : ";\n");
        }
        // Methods become member functions. A leading `self` param is implicit (it
        // is `*this`); the struct stays a C++ aggregate (member functions are
        // allowed), so `Name{...}` construction is unaffected.
        for (const StmtPtr& m : sd.methods) {
            emit_doc(os, m->doc, "    ");
            gen_method(os, static_cast<const FnDef&>(*m));
        }
        // A pretty-printer member: `io.print` renders a struct on multiple indented lines
        // (nice + readable by default); `io.rprint` and `str()` keep the compact operator<<
        // form. Only for streamable structs, and a member (not a free function) so `io.print`
        // detects it with a simple `requires` and there are no ADL surprises.
        if (streamable) {
            os << "    void cheatah_pretty_print(std::ostream& os_, long long indent_) const {\n";
            os << "        os_ << \"" << sd.name << "(\\n\";\n";
            for (std::size_t i = 0; i < sd.fields.size(); ++i) {
                const Field& f = sd.fields[i];
                os << "        os_ << std::string(indent_ + 4, ' ') << \"" << f.name << " = \";\n";
                if (struct_fields_.contains(f.type.name))  // a (streamable) struct field -> recurse
                    os << "        this->" << cpp_ident(f.name) << ".cheatah_pretty_print(os_, indent_ + 4);\n";
                else if (is_char_width(f.type.name))  // i8/u8 -> promote so it prints as a number
                    os << "        os_ << +this->" << cpp_ident(f.name) << ";\n";
                else
                    os << "        os_ << this->" << cpp_ident(f.name) << ";\n";
                os << "        os_ << \"" << (i + 1 < sd.fields.size() ? "," : "") << "\\n\";\n";
            }
            os << "        os_ << std::string(indent_, ' ') << \")\";\n";
            os << "    }\n";
        }
        os << "};\n";
        // `struct S : Iface…` -> a compile-time check that S fulfills each interface.
        if (!sd.fulfills.empty()) {
            os << "static_assert(";
            for (std::size_t i = 0; i < sd.fulfills.size(); ++i) {
                os << (i != 0 ? " && " : "") << sd.fulfills[i] << "<" << sd.name << ">";
            }
            os << ", \"" << sd.name << " must fulfill ";
            for (std::size_t i = 0; i < sd.fulfills.size(); ++i) {
                os << (i != 0 ? ", " : "") << sd.fulfills[i];
            }
            os << "\");\n";
        }
        // A Python-repr-style operator<< (the COMPACT form: `Name(f1=…, f2=…)`) used by
        // str() and io.rprint. Emitted only when every field is itself streamable, so a struct
        // with a container field doesn't break compilation of programs that use it.
        if (streamable) {
            os << "inline std::ostream& operator<<(std::ostream& os_, const " << sd.name
               << "& v_) {\n";
            os << "    return os_ << \"" << sd.name << "(\"";
            for (std::size_t i = 0; i < sd.fields.size(); ++i) {
                os << " << \"" << (i == 0 ? "" : ", ") << sd.fields[i].name << "=\" << "
                   << (is_char_width(sd.fields[i].type.name) ? "+v_." : "v_.")
                   << cpp_ident(sd.fields[i].name);
            }
            os << " << \")\";\n";
            os << "}\n";
        }
        os << "\n";
    }

    // Whether @p t streams via operator<< (so a struct with only such fields gets an
    // auto-generated operator<<): the scalar primitives, or a struct whose fields are all
    // themselves streamable. Containers (list/dict/array) are not.
    bool type_is_streamable(const TypeRef& t, std::set<std::string>& visiting) const {
        if (t.name == "int" || t.name == "float" || t.name == "str" || t.name == "bool")
            return true;
        if (width_cpp_type(t.name)) return true;  // an explicit-width scalar streams (numerically — see is_char_width)
        const auto it = struct_fields_.find(t.name);
        if (it == struct_fields_.end()) return false;  // container / unknown -> not streamable
        if (!visiting.insert(t.name).second) return true;  // recursive struct: assume ok
        bool ok = true;
        for (const Field& f : it->second)
            if (!type_is_streamable(f.type, visiting)) { ok = false; break; }
        visiting.erase(t.name);
        return ok;
    }
    bool struct_is_streamable(const StructDef& sd) const {
        std::set<std::string> visiting{sd.name};
        for (const Field& f : sd.fields)
            if (!type_is_streamable(f.type, visiting)) return false;
        return true;
    }

    // Emit `Name{.f = static_cast<T>(v), …}` — a C++20 designated initializer. Each field is
    // validated against the struct (an unknown field is a codegen error) and each value is
    // cast to the field's type, so `.what = 10` into a `float` field is a clean conversion
    // rather than a braced-init narrowing error. Fields not listed default-initialize.
    std::string gen_struct_init(const std::string& name, const StructInit& si,
                                const std::string& indent) {
        const auto it = struct_fields_.find(name);
        // A multi-line source initializer stays multi-line (one field per indented line), as
        // for list/dict literals and call arguments — readable .gen.cpp.
        const bool ml = si.multiline && !si.fields.empty();
        const std::string inner = indent + "    ";
        std::string out = name + "{";
        for (std::size_t i = 0; i < si.fields.size(); ++i) {
            std::string ftype;
            bool ftype_width = false;
            if (it != struct_fields_.end()) {
                for (const Field& f : it->second)
                    if (f.name == si.fields[i]) { ftype = map_declared_type(f.type); ftype_width = type_uses_width(f.type); break; }
            }
            // A width-typed container field (`x: list[u8]`) gets the field type as the literal's
            // construction type, so `.x = static_cast<vector<uint8_t>>(vector<uint8_t>{…})` is a
            // valid identity cast rather than a cross-type cast from a CTAD'd vector<long long>.
            const bool lit = si.values[i]->kind == ExprKind::ListLit || si.values[i]->kind == ExprKind::DictLit;
            const std::string* hint = (ftype_width && lit) ? &ftype : nullptr;
            const std::string val = gen_expr(*si.values[i], ml ? inner : indent, hint);
            std::string entry = "." + cpp_ident(si.fields[i]) + " = ";
            if (ftype.empty()) {
                diags_.push_back("codegen: struct '" + name + "' has no field '" + si.fields[i] +
                                 "'");
                entry += val;
            } else if (si.values[i]->kind == ExprKind::NumberLit &&
                       ((ftype == "long long" && val.ends_with("LL")) ||
                        (ftype == "double" && !val.ends_with("LL")))) {
                entry += val;  // a literal already of the field type: no cast to itself (readability-redundant-casting)
            } else {
                entry += "static_cast<";
                entry += ftype;
                entry += ">(";
                entry += val;
                entry += ")";
            }
            if (ml) {
                out += "\n";
                out += inner;
                out += entry;
                if (i + 1 < si.fields.size()) out += ",";
            } else {
                if (i != 0) out += ", ";
                out += entry;
            }
        }
        if (ml) out += "\n" + indent;
        return out + "}";
    }

    // The type declared for a method param (parallel to params; the implicit `self`
    // at index 0 has none), or "" when untyped.
    static std::string method_param_type(const FnDef& fd, std::size_t i) {
        return i < fd.param_types.size() ? fd.param_types[i] : std::string();
    }

    void gen_method(std::ostringstream& os, const FnDef& fd) {
        const bool has_self = !fd.params.empty() && fd.params[0] == "self";
        os << "    auto " << cpp_ident(fd.name) << "(";
        bool first = true;
        for (std::size_t i = (has_self ? 1 : 0); i < fd.params.size(); ++i) {
            os << (first ? "" : ", ") << param_prefix(method_param_type(fd, i)) << cpp_ident(fd.params[i]);
            first = false;
        }
        // A method that never assigns through `self` is `const`, so it works on
        // const objects — required for the print/`str()` protocol and read-only use.
        const bool mutates = has_self && block_mutates_self(fd.body);
        os << ")" << (mutates ? "" : " const") << " {\n";
        const bool prev = in_method_;
        in_method_ = true;
        deferred_lets_.clear(); const_vars_.clear();  // no-value lets are scoped to this method body
        gen_block(os, fd.body, "        ");
        in_method_ = prev;
        os << "    }\n";
    }

    // Emit a call that used `name = value` keyword arguments: positional args fill the leading
    // parameters, keywords land by NAME, and any remaining slot takes the parameter's declared
    // default. Only program-defined functions are addressable by keyword (the compiler knows
    // their signatures); module functions and methods need positional arguments.
    std::string gen_kwargs_call(const Call& c, const std::string& indent) {
        if (c.callee->kind != ExprKind::Ident || !fn_defs_.contains(static_cast<const Ident&>(*c.callee).name)) {
            diags_.emplace_back("codegen: keyword arguments require a program-defined function");
            return "/*kwargs error*/";
        }
        const FnDef& fd = *fn_defs_.at(static_cast<const Ident&>(*c.callee).name);
        std::vector<std::string> slot(fd.params.size());
        std::vector<bool> filled(fd.params.size(), false);
        std::size_t next_positional = 0;
        for (std::size_t i = 0; i < c.args.size(); ++i) {
            std::size_t target = fd.params.size();
            if (c.arg_names[i].empty()) {
                target = next_positional++;
            } else {
                for (std::size_t j = 0; j < fd.params.size(); ++j) {
                    if (fd.params[j] == c.arg_names[i]) target = j;
                }
            }
            if (target >= fd.params.size()) {
                diags_.push_back("codegen: '" + fd.name + "' has no parameter '" +
                                 c.arg_names[i] + "'");
                return "/*kwargs error*/";
            }
            if (filled[target]) {
                diags_.push_back("codegen: parameter '" + fd.params[target] +
                                 "' of '" + fd.name + "' given more than once");
                return "/*kwargs error*/";
            }
            slot[target] = gen_expr(*c.args[i], indent);
            filled[target] = true;
        }
        for (std::size_t j = 0; j < fd.params.size(); ++j) {
            if (filled[j]) continue;
            if (j < fd.param_defaults.size() && fd.param_defaults[j]) {
                slot[j] = gen_expr(*fd.param_defaults[j], indent);  // the declared default
            } else {
                diags_.push_back("codegen: call to '" + fd.name + "' is missing required parameter '" +
                                 fd.params[j] + "'");
                return "/*kwargs error*/";
            }
        }
        std::string out = fd.name + "(";
        for (std::size_t j = 0; j < slot.size(); ++j) out += (j ? ", " : "") + slot[j];
        return out + ")";
    }

    // `if (x == y)`, not `if ((x == y))`: a binary expression renders parenthesized, and the
    // statement adds its own pair — drop the redundant one when it wraps the whole condition
    // (clang -Wparentheses-equality on the emitted C++).
    static std::string bare_cond(std::string s) {
        if (s.size() < 2 || s.front() != '(' || s.back() != ')') return s;
        int depth = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '(') ++depth;
            else if (s[i] == ')' && --depth == 0 && i + 1 != s.size()) return s;  // closes early: not one outer pair
        }
        return s.substr(1, s.size() - 2);
    }

    void gen_fn(std::ostringstream& os, const FnDef& fd) {
        // `static` -> internal linkage, so the optimizer inlines/optimizes these
        // like local C++ functions (no exported-symbol interposition barrier).
        // Untyped params -> C++20 abbreviated function template; an interface-typed
        // param becomes a concept-constrained `auto` (static dispatch). A `-> Type` hint
        // pins the return type (the C++ backend then checks every `return` against it);
        // absent, the return stays `auto`.
        os << "static " << (fd.is_constexpr ? "constexpr " : "") << return_type_cpp(fd.return_type)
           << " " << cpp_ident(fd.name) << "(";
        for (std::size_t i = 0; i < fd.params.size(); ++i) {
            os << (i != 0 ? ", " : "") << param_prefix(method_param_type(fd, i)) << cpp_ident(fd.params[i]);
        }
        os << ") {\n";
        deferred_lets_.clear(); const_vars_.clear();  // no-value lets are scoped to this function body
        return_type_hint_ = spelling_uses_width(fd.return_type) ? return_type_cpp(fd.return_type) : "";
        gen_block(os, fd.body, "    ");
        return_type_hint_.clear();
        os << "}\n\n";

        // Default parameters lower to FORWARDING OVERLOADS, one per trailing-default suffix —
        // a C++ default argument on an `auto` parameter cannot drive deduction, an overload can.
        for (std::size_t cut = fd.params.size(); cut-- > 0;) {
            if (cut >= fd.param_defaults.size() || !fd.param_defaults[cut]) {
                break;  // defaults are trailing; the first non-default ends the ladder
            }
            // Each synthesized overload is public API, so in library mode it carries its own
            // Javadoc (the gate requires 100% docs): a brief pointing at the primary, @param for
            // the args it keeps, @return when the primary documents one. (emit_doc is a no-op in
            // program mode, so a program's .gen.cpp stays lean.)
            std::string ov_doc = "Convenience overload of `" + fd.name +
                                 "` with the trailing default argument(s) applied.";
            for (std::size_t i = 0; i < cut; ++i)
                ov_doc += "\n@param " + fd.params[i] + " as documented on the primary overload.";
            if (fd.doc.find("@return") != std::string::npos)
                ov_doc += "\n@return as the primary overload.";
            emit_doc(os, ov_doc);
            os << "static " << (fd.is_constexpr ? "constexpr " : "") << return_type_cpp(fd.return_type)
           << " " << cpp_ident(fd.name) << "(";
            for (std::size_t i = 0; i < cut; ++i) {
                os << (i != 0 ? ", " : "") << param_prefix(method_param_type(fd, i)) << cpp_ident(fd.params[i]);
            }
            os << ") { return " << cpp_ident(fd.name) << "(";
            for (std::size_t i = 0; i < cut; ++i) {
                os << (i != 0 ? ", " : "") << cpp_ident(fd.params[i]);
            }
            for (std::size_t i = cut; i < fd.params.size(); ++i) {
                os << (i != 0 ? ", " : "") << gen_expr(*fd.param_defaults[i]);
            }
            os << "); }\n";
        }
        os << "\n";
    }

    // Whether expression @p e contains a call anywhere — a conservative "may have side
    // effects" test for deciding whether a dead `let`'s initializer must still run.
    bool has_call(const Expr& e) const {
        switch (e.kind) {
            case ExprKind::Call: return true;
            case ExprKind::Member: return has_call(*static_cast<const Member&>(e).object);
            case ExprKind::Index: {
                const auto& ix = static_cast<const Index&>(e);
                if (has_call(*ix.object) || has_call(*ix.index)) return true;
                return std::ranges::any_of(ix.extra, [&](const ExprPtr& more) { return has_call(*more); });
            }
            case ExprKind::Slice: {
                const auto& sl = static_cast<const Slice&>(e);
                return has_call(*sl.object) || (sl.start && has_call(*sl.start)) ||
                       (sl.stop && has_call(*sl.stop));
            }
            case ExprKind::Unary: return has_call(*static_cast<const Unary&>(e).operand);
            case ExprKind::Binary: {
                const auto& b = static_cast<const Binary&>(e);
                return has_call(*b.lhs) || has_call(*b.rhs);
            }
            case ExprKind::ListLit: {
                const auto calls = [&](const ExprPtr& el) { return has_call(*el); };
                return std::ranges::any_of(static_cast<const ListLit&>(e).elements, calls);
            }
            case ExprKind::DictLit: {
                const auto& d = static_cast<const DictLit&>(e);
                const auto calls = [&](const ExprPtr& x) { return has_call(*x); };
                return std::ranges::any_of(d.keys, calls) || std::ranges::any_of(d.values, calls);
            }
            case ExprKind::StructInit: {
                const auto calls = [&](const ExprPtr& v) { return has_call(*v); };
                return std::ranges::any_of(static_cast<const StructInit&>(e).values, calls);
            }
            default: return false;  // literals, identifiers
        }
    }

    // Whether statement @p s READS variable @p name (recursing through nested blocks). An
    // assignment to a plain Ident target is a WRITE, not a read (its value/other exprs may
    // still read); a `let`'s initializer is a read of names it references. Drives dead-local
    // elimination: a `let x = …` whose x is never read afterwards can be dropped.
    bool stmt_reads_var(const Stmt& s, const std::string& name) const {
        switch (s.kind) {
            case StmtKind::Let:
                return refers_to(*static_cast<const Let&>(s).value, name);
            case StmtKind::Assign: {
                const auto& a = static_cast<const Assign&>(s);
                const bool target_read =
                    a.target->kind != ExprKind::Ident && refers_to(*a.target, name);
                return target_read || refers_to(*a.value, name);
            }
            case StmtKind::ExprStmt:
                return refers_to(*static_cast<const ExprStmt&>(s).expr, name);
            case StmtKind::Return: {
                const auto& r = static_cast<const Return&>(s);
                return r.value && refers_to(*r.value, name);
            }
            case StmtKind::If: {
                const auto& n = static_cast<const If&>(s);
                return refers_to(*n.cond, name) || block_reads_var(n.then_body, name) ||
                       block_reads_var(n.else_body, name);
            }
            case StmtKind::While: {
                const auto& w = static_cast<const While&>(s);
                return refers_to(*w.cond, name) || block_reads_var(w.body, name);
            }
            case StmtKind::For: {
                const auto& f = static_cast<const For&>(s);
                return refers_to(*f.iterable, name) || block_reads_var(f.body, name);
            }
            case StmtKind::With: {
                const auto& w = static_cast<const With&>(s);
                return refers_to(*w.resource, name) || block_reads_var(w.body, name);
            }
            case StmtKind::Try: {
                const auto& t = static_cast<const Try&>(s);
                if (block_reads_var(t.body, name)) return true;
                for (const Handler& h : t.handlers) {
                    if (h.kind && refers_to(*h.kind, name)) return true;
                    if (block_reads_var(h.body, name)) return true;
                }
                return t.has_finally && block_reads_var(t.finally_body, name);
            }
            case StmtKind::Match: {
                const auto& m = static_cast<const Match&>(s);
                if (refers_to(*m.subject, name)) return true;
                return std::ranges::any_of(m.cases, [&](const MatchCase& c) {
                    // A wildcard case (`case _`) carries no pattern expression.
                    return (c.pattern && refers_to(*c.pattern, name)) || block_reads_var(c.body, name);
                });
            }
            case StmtKind::Raise: {
                const auto& r = static_cast<const Raise&>(s);
                return r.value && refers_to(*r.value, name);   // a bare re-raise reads nothing
            }
            default:
                return false;  // Import/StructDef/FnDef/EnumDef/InterfaceDef/RawCpp/Break/Continue
        }
    }
    bool block_reads_var(const Block& b, const std::string& name) const {
        return std::ranges::any_of(b, [&](const StmtPtr& s) { return stmt_reads_var(*s, name); });
    }

    // If @p s is a `let name = init` whose variable is never read in @p block at/after
    // @p from, emit the MINIMAL equivalent and return true: the initializer as an expression
    // statement when it may have side effects (so `let A = solve_system()` keeps the call as
    // `solve_system();`), or nothing at all for a side-effect-free initializer. Returns false
    // when the variable IS used (the caller then emits the `let` normally).
    bool try_drop_dead_let(std::ostringstream& os, const Stmt& s, const Block& block,
                           std::size_t from, const std::string& indent) {
        const auto& l = static_cast<const Let&>(s);
        if (l.name.empty() || !l.value) return false;
        for (std::size_t i = from; i < block.size(); ++i)
            if (stmt_reads_var(*block[i], l.name)) return false;  // used -> keep the binding
        if (line_directives_ && s.line != 0)
            os << "#line " << s.line << " \"" << source_file_ << "\"\n";
        if (has_call(*l.value))  // keep side effects, drop only the unused variable
            os << indent << gen_expr(*l.value, indent) << ";\n";
        return true;  // pure initializer -> emit nothing
    }

    void gen_block(std::ostringstream& os, const Block& block, const std::string& indent) {
        for (std::size_t i = 0; i < block.size(); ++i) {
            if (remove_unused_ && block[i]->kind == StmtKind::Let &&
                try_drop_dead_let(os, *block[i], block, i + 1, indent)) {
                continue;
            }
            gen_stmt(os, *block[i], indent);
        }
    }

    // An assignment target is an lvalue: `xs[i] = v` / `d[k] = v` must use a raw
    // subscript (not the by-value `index()` helper used in value position).
    std::string gen_lvalue(const Expr& e) {
        if (e.kind == ExprKind::Index) {
            const auto& ix = static_cast<const Index&>(e);
            if (!ix.extra.empty()) {  // x[i, j, ...] = v  -> mutable multi-index ref
                std::string out = gen_expr(*ix.object) + ".item_ref(" + gen_expr(*ix.index);
                for (const ExprPtr& more : ix.extra) out += ", " + gen_expr(*more);
                return out + ")";
            }
            return gen_expr(*ix.object) + "[" + gen_expr(*ix.index) + "]";
        }
        return gen_expr(e);
    }

    void gen_stmt(std::ostringstream& os, const Stmt& s, const std::string& indent) {
        // Map this statement back to its .purr line so the C++ backend's diagnostics (and
        // the editor's, via `purrc --check`) report the original source location. Emitted
        // only in check mode (set_source_file), so a normal compile's .gen.cpp stays clean.
        if (line_directives_ && s.line != 0) {
            os << "#line " << s.line << " \"" << source_file_ << "\"\n";
        }
        switch (s.kind) {
            case StmtKind::Let: {
                const auto& l = static_cast<const Let&>(s);
                if (!l.value) {
                    // `let x` with no initializer: defer the declaration. It is realized as
                    // `auto x = …` at its first assignment (below); if it is never assigned it
                    // is simply never emitted (removed). Using it without a value, or with only
                    // a conditional assignment, fails to compile (it has no declaration in
                    // scope) — an unset variable is a bug in cheatah.
                    deferred_lets_.insert(l.name);
                    return;
                }
                // `constexpr let` -> a C++ `constexpr` binding AND a recorded compile-time
                // constant, so `if`/`match` over it auto-lower to `if constexpr` (the C++
                // compiler enforces the initializer really is constant). One keyword:
                // `constexpr` (consteval stays behind a cpp{} escape hatch).
                const char* cx = l.is_constexpr ? "constexpr " : "";
                if (l.is_constexpr) const_vars_.insert(l.name);
                if (l.has_type && !l.type.qualified.empty()) {
                    // A module-qualified declared type (`let v: fixarray.Fixed<f32, 3> = …`,
                    // `let s: state.State = …`): render via the module-aware mapper and assign the
                    // initializer straight through (these are value/handle types, not containers).
                    os << indent << cx << map_type_string(l.type.qualified) << " " << cpp_ident(l.name)
                       << " = " << gen_expr(*l.value, indent) << ";\n";
                } else if (l.has_type) {
                    // An explicit type drives the declaration, so empty `[]` / `{}`
                    // get their element types from the annotation (not deduced).
                    const bool empty_list =
                        l.value->kind == ExprKind::ListLit &&
                        static_cast<const ListLit&>(*l.value).elements.empty();
                    const bool empty_dict =
                        l.value->kind == ExprKind::DictLit &&
                        static_cast<const DictLit&>(*l.value).keys.empty();
                    if (empty_list || empty_dict) {
                        os << indent << cx << map_type(l.type) << " " << cpp_ident(l.name) << ";\n";
                    } else if (l.type.name == "ndarray" && l.type.args.size() == 1 &&
                               type_uses_width(l.type)) {
                        // A narrow-element ndarray declared type DRIVES construction: wrap the
                        // initializer in astype<W> so `let a: ndarray<i8> = ndarray.array([…])`
                        // yields a basic_ndarray<int8_t> directly (the element widths differ, so a
                        // plain assignment would not convert). astype is identity-cheap when the
                        // element already matches. This is how the sized-int win reaches ndarray.
                        os << indent << cx << map_type(l.type) << " " << cpp_ident(l.name)
                           << " = ::cheatah::ndarray::astype<" << map_type(l.type.args[0]) << ">("
                           << gen_expr(*l.value, indent) << ");\n";
                    } else {
                        // A width-typed container literal (`let v: list[i8] = […]`) is built AS the
                        // declared type so CTAD can't mis-deduce vector<long long>. A plain int/float
                        // element deduces correctly, so it is left on the CTAD path (no output churn).
                        const std::string decl = map_type(l.type);
                        const std::string* hint = type_uses_width(l.type) ? &decl : nullptr;
                        os << indent << cx << decl << " " << cpp_ident(l.name) << " = "
                           << gen_expr(*l.value, indent, hint) << ";\n";
                    }
                } else {
                    os << indent << cx << "auto " << cpp_ident(l.name) << " = " << gen_expr(*l.value, indent)
                       << ";\n";
                }
                return;
            }
            case StmtKind::Assign: {
                const auto& a = static_cast<const Assign&>(s);
                // Realize a deferred `let x` (declared with no value) at its FIRST assignment:
                // emit the declaration `auto x = …` here, so the variable comes into being
                // exactly where it is first given a value. Later assignments are plain `x = …`.
                if (a.op == "=" && a.target->kind == ExprKind::Ident) {
                    const std::string& tname = static_cast<const Ident&>(*a.target).name;
                    if (deferred_lets_.erase(tname)) {
                        os << indent << "auto " << cpp_ident(tname) << " = " << gen_expr(*a.value, indent)
                           << ";\n";
                        return;
                    }
                }
                // Self-append fast path: `x = x + e1 + e2 …` -> `x += e1; x += e2; …`,
                // so building a string (or accumulator) doesn't copy the whole left
                // operand each step (O(n²) -> O(n); no full-length temporaries). Only
                // when the target is a plain variable that sits at the head of the `+`
                // chain and is not re-read by the appended operands.
                if (a.op == "=" && a.target->kind == ExprKind::Ident &&
                    a.value->kind == ExprKind::Binary &&
                    static_cast<const Binary&>(*a.value).op == "+") {
                    const std::string& name = static_cast<const Ident&>(*a.target).name;
                    std::vector<const Expr*> ops;
                    flatten_add(*a.value, ops);
                    bool safe = ops.size() > 1 && ops[0]->kind == ExprKind::Ident &&
                                static_cast<const Ident&>(*ops[0]).name == name;
                    for (std::size_t i = 1; safe && i < ops.size(); ++i) {
                        if (refers_to(*ops[i], name)) safe = false;
                    }
                    if (safe) {
                        // Collapse the appends into ONE chained statement when possible:
                        //   `((x += a) += b) += c;`
                        // `operator+=` returns a reference to `x` — true for std::string
                        // and every arithmetic accumulator, the only types a `+` self-append
                        // fires on — so the appends chain in place: no temporaries, no
                        // repeated `x +=`. A string literal appends as a bare `const char*`
                        // (no throwaway std::string built just to append a literal).
                        std::string expr;
                        for (std::size_t i = 1; i < ops.size(); ++i) {
                            const std::string operand =
                                ops[i]->kind == ExprKind::StringLit
                                    ? cpp_string_literal(static_cast<const StringLit&>(*ops[i]).value)
                                    : gen_expr(*ops[i]);
                            if (i == 1) {
                                expr = cpp_ident(name);
                                expr += " += ";
                            } else {
                                expr.insert(0, "(");
                                expr += ") += ";
                            }
                            expr += operand;
                        }
                        os << indent << expr << ";\n";
                        return;
                    }
                }
                os << indent << gen_lvalue(*a.target) << " " << a.op << " "
                   << gen_expr(*a.value, indent) << ";\n";
                return;
            }
            case StmtKind::If: {
                const auto& n = static_cast<const If&>(s);
                // Explicit `if constexpr`, OR auto: a condition that's a known compile-time
                // constant (literals / `constexpr let` names) lowers to `if constexpr` too.
                const bool cx = n.is_constexpr || expr_is_const(*n.cond);
                os << indent << (cx ? "if constexpr (" : "if (")
                   << bare_cond(gen_expr(*n.cond)) << ") {\n";
                gen_block(os, n.then_body, indent + "    ");
                os << indent << "}";
                if (!n.else_body.empty()) {
                    os << " else {\n";
                    gen_block(os, n.else_body, indent + "    ");
                    os << indent << "}";
                }
                os << "\n";
                return;
            }
            case StmtKind::While: {
                const auto& w = static_cast<const While&>(s);
                os << indent << "while (" << bare_cond(gen_expr(*w.cond)) << ") {\n";
                gen_block(os, w.body, indent + "    ");
                os << indent << "}\n";
                return;
            }
            case StmtKind::For:
                gen_for(os, static_cast<const For&>(s), indent);
                return;
            case StmtKind::With: {
                // with <resource> [as <name>] { … }  ->  a plain nested block that binds the
                // resource to a local, so its RAII destructor runs when the block exits. When
                // there is no `as`, bind to a hidden local (a discarded temporary would be
                // destroyed immediately, defeating the scope guard).
                const auto& w = static_cast<const With&>(s);
                const std::string name =
                    w.bind.empty() ? "_purr_with_" + std::to_string(with_id_++) : w.bind;
                os << indent << "{\n";
                os << indent << "    auto " << cpp_ident(name) << " = " << gen_expr(*w.resource) << ";\n";
                gen_block(os, w.body, indent + "    ");
                os << indent << "}\n";
                return;
            }
            case StmtKind::Return: {
                const auto& r = static_cast<const Return&>(s);
                // A `return [..]` / `return {..}` whose function returns a width-typed container is
                // built AS that type (return_type_hint_ is set only then), so CTAD can't mis-deduce.
                const bool lit = r.value && (r.value->kind == ExprKind::ListLit ||
                                             r.value->kind == ExprKind::DictLit);
                const std::string* hint = (lit && !return_type_hint_.empty()) ? &return_type_hint_ : nullptr;
                os << indent << "return" << (r.value ? " " + gen_expr(*r.value, indent, hint) : "") << ";\n";
                return;
            }
            case StmtKind::Try: {
                const auto& t = static_cast<const Try&>(s);
                std::string body_indent = indent;
                // `finally` is a scope guard, not a duplicated block: emitting the body twice (once on
                // the normal path, once on the throwing one) would silently skip it on `return`, `break`
                // and `continue`, which is exactly when cleanup matters most.
                if (t.has_finally) {
                    os << indent << "{\n";
                    os << indent << "    auto _purr_finally = ::cheatah::builtins::make_finally([&] {\n";
                    gen_block(os, t.finally_body, indent + "        ");
                    os << indent << "    });\n";
                    body_indent = indent + "    ";
                }
                os << body_indent << "try {\n";
                gen_block(os, t.body, body_indent + "    ");
                // ONE `catch (...)` rather than a catch per handler. Normalizing through current_error()
                // means a raised Error, any std::exception, and a throw of a type we have never heard of
                // all reach the same dispatch — that last case used to sail past every handler and take
                // the process with it.
                os << body_indent << "} catch (...) {\n";
                const std::string ei = body_indent + "    ";
                os << ei << "const ::cheatah::builtins::Error _purr_err = ::cheatah::builtins::current_error();\n";
                bool has_catch_all = false;
                for (std::size_t i = 0; i < t.handlers.size(); ++i) {
                    const Handler& h = t.handlers[i];
                    const bool is_catch_all = (h.kind == nullptr);
                    std::string open = ei;
                    if (is_catch_all) {
                        has_catch_all = true;
                        open += (i == 0 ? "{\n" : "else {\n");
                    } else {
                        open += (i == 0 ? "if (" : "else if (");
                        open += "_purr_err.kind() == " + gen_expr(*h.kind) + ") {\n";
                    }
                    os << open;
                    if (!h.var.empty()) {
                        os << ei << "    const auto& " << cpp_ident(h.var) << " = _purr_err;\n";
                    }
                    gen_block(os, h.body, ei + "    ");
                    os << ei << "}\n";
                    if (is_catch_all) break;   // nothing after a catch-all can ever run
                }
                // An error no handler claimed keeps travelling. Swallowing it here would turn a
                // `try/except` that names one kind into a blanket suppressor of every other failure.
                if (!has_catch_all) {
                    os << ei << (t.handlers.empty() ? "" : "else ") << "throw;\n";
                }
                os << body_indent << "}\n";
                if (t.has_finally) os << indent << "}\n";
                return;
            }
            case StmtKind::Raise: {
                const auto& r = static_cast<const Raise&>(s);
                // A bare `raise` re-raises what is being handled; C++ `throw;` does exactly that, and
                // preserves the original type rather than reconstructing an approximation of it.
                if (!r.value) {
                    os << indent << "throw;\n";
                    return;
                }
                os << indent << "throw ::cheatah::builtins::Error(" << gen_expr(*r.value) << ");\n";
                return;
            }
            case StmtKind::ExprStmt:
                os << indent << gen_expr(*static_cast<const ExprStmt&>(s).expr, indent) << ";\n";
                return;
            case StmtKind::RawCpp:
                // A `cpp { … }` block inside a function/block: emit its body inline,
                // verbatim, at this point in the enclosing scope.
                os << static_cast<const RawCpp&>(s).code << "\n";
                return;
            case StmtKind::Break:
                os << indent << "break;\n";
                return;
            case StmtKind::Continue:
                os << indent << "continue;\n";
                return;
            case StmtKind::Match:
                gen_match(os, static_cast<const Match&>(s), indent);
                return;
            case StmtKind::Import:
            case StmtKind::StructDef:
            case StmtKind::FnDef:
            case StmtKind::InterfaceDef:
            case StmtKind::EnumDef:
                return;  // emitted at file scope
        }
    }

    // Whether @p e is a compile-time constant in the emitted C++ — built only from literals,
    // `constexpr let` names (const_vars_), and operators that stay constant (no runtime helper
    // calls). Drives AUTO-lowering of `if`/`match` over a known constant to their `if constexpr`
    // form. Conservative: strings, function calls, and the helper-lowered ops (`/ // % ** in`)
    // count as non-constant, so we never emit an `if constexpr` the C++ compiler would reject.
    bool expr_is_const(const Expr& e) const {
        switch (e.kind) {
            case ExprKind::NumberLit:
            case ExprKind::BoolLit:
                return true;
            case ExprKind::Ident:
                return const_vars_.contains(static_cast<const Ident&>(e).name);
            case ExprKind::Unary: {
                const auto& u = static_cast<const Unary&>(e);
                return (u.op == "-" || u.op == "+" || u.op == "!") && expr_is_const(*u.operand);
            }
            case ExprKind::Binary: {
                const auto& b = static_cast<const Binary&>(e);
                static const std::set<std::string> kConstOps = {
                    "+", "-", "*", "==", "!=", "<", "<=", ">", ">=", "&&", "||"};
                return kConstOps.contains(b.op) && expr_is_const(*b.lhs) && expr_is_const(*b.rhs);
            }
            default:
                return false;
        }
    }
    // Every non-wildcard case pattern of @p m is a compile-time constant (so the whole match
    // can lower to an `if constexpr` chain over a constant subject).
    bool match_patterns_all_const(const Match& m) const {
        return std::ranges::all_of(m.cases, [&](const MatchCase& c) {
            return c.wildcard || expr_is_const(*c.pattern);
        });
    }

    // A case pattern that's a valid C++ `switch` label: an integer (not float) literal,
    // optionally signed (`case -1`). Strings/floats/bools/identifiers/runtime exprs are
    // NOT — a `switch` can't express those, so they keep the `==` if/else-if chain.
    static bool is_int_switch_label(const Expr& e) {
        if (e.kind == ExprKind::NumberLit) {
            const std::string& t = static_cast<const NumberLit&>(e).text;
            return t.find('.') == std::string::npos && t.find('e') == std::string::npos &&
                   t.find('E') == std::string::npos;  // integer literal, not a double
        }
        if (e.kind == ExprKind::Unary) {  // `case -1` / `case +1`
            const auto& u = static_cast<const Unary&>(e);
            return (u.op == "-" || u.op == "+") && is_int_switch_label(*u.operand);
        }
        return false;
    }
    // Whether a (non-constexpr) match can lower to a real C++ switch: ≥1 concrete case and
    // every non-wildcard pattern is an integer-literal label the compiler will accept.
    static bool match_is_switchable(const Match& m) {
        bool any = false;
        for (const MatchCase& c : m.cases) {
            if (c.wildcard) continue;
            if (!is_int_switch_label(*c.pattern)) return false;
            any = true;
        }
        return any;
    }
    // A block whose last statement unconditionally leaves the enclosing block (so a trailing
    // `break;` in a switch case would be unreachable). Lets us omit that break and dodge
    // -Wunreachable-code on `case k { return … }`.
    static bool block_falls_through(const Block& body) {
        if (body.empty()) return true;
        switch (body.back()->kind) {
            case StmtKind::Return:
            case StmtKind::Break:
            case StmtKind::Continue:
            case StmtKind::Raise:
                return false;
            default:
                return true;
        }
    }

    // Lower a `match`. Three shapes, picked so the C++ backend always compiles:
    //   • `constexpr match` -> a `constexpr` copy of the subject + an `if constexpr` chain,
    //     so the dead arms are discarded at COMPILE time (C++ has no `switch constexpr`).
    //   • plain match with integer-literal cases -> a real C++ `switch` (each arm braced +
    //     `break`; no fallthrough, matching match's one-arm semantics; `_` -> `default:`).
    //   • plain match on anything else (strings, floats, runtime values) -> evaluate the
    //     subject once, then an `if / else-if` chain comparing with `==`; `_` is the else.
    void gen_match(std::ostringstream& os, const Match& m, const std::string& indent) {
        // `_purr_match_N`, like `_purr_with_N`/`_purr_err`: block-scope, so a leading
        // underscore + lowercase is legal — a double underscore would be RESERVED
        // ([lex.name]; clang-tidy cert-dcl51-cpp), in every consumer of the emitted C++.
        const std::string var = "_purr_match_" + std::to_string(match_id_++);
        const std::string in = indent + "    ";
        os << indent << "{\n";

        // Explicit `constexpr match`, OR auto: a constant subject whose every case label is
        // also constant -> a compile-time `if constexpr` chain over a `constexpr` copy.
        const bool cx = m.is_constexpr || (expr_is_const(*m.subject) && match_patterns_all_const(m));
        if (cx) {
            os << in << "constexpr auto " << var << " = " << gen_expr(*m.subject) << ";\n";
            const MatchCase* wildcard = nullptr;
            bool first = true;
            for (const MatchCase& c : m.cases) {
                if (c.wildcard) { wildcard = &c; continue; }
                os << in << (first ? "if constexpr (" : "else if constexpr (") << var << " == "
                   << gen_expr(*c.pattern) << ") {\n";
                gen_block(os, c.body, in + "    ");
                os << in << "}\n";
                first = false;
            }
            if (wildcard != nullptr) {
                os << in << (first ? "{\n" : "else {\n");
                gen_block(os, wildcard->body, in + "    ");
                os << in << "}\n";
            }
            os << indent << "}\n";
            return;
        }

        if (match_is_switchable(m)) {
            os << in << "auto " << var << " = " << gen_expr(*m.subject) << ";\n";
            os << in << "switch (" << var << ") {\n";
            const std::string cin = in + "    ";
            const MatchCase* wildcard = nullptr;
            // Consecutive arms with byte-identical bodies share one case label group
            // (`case A: case B: { … }`) instead of two clone branches (bugprone-branch-clone).
            std::string pending_labels;
            std::string pending_body;
            const auto flush = [&] {
                if (pending_labels.empty()) return;
                os << pending_labels << "{\n" << pending_body << cin << "}\n";
                pending_labels.clear();
                pending_body.clear();
            };
            for (const MatchCase& c : m.cases) {
                if (c.wildcard) { wildcard = &c; continue; }
                std::ostringstream body;
                gen_block(body, c.body, cin + "    ");
                if (block_falls_through(c.body)) body << cin << "    break;\n";
                if (!pending_labels.empty() && body.str() != pending_body) flush();
                pending_labels += cin + "case " + gen_expr(*c.pattern) + ": ";
                pending_body = body.str();
            }
            flush();
            if (wildcard != nullptr) {
                os << cin << "default: {\n";
                gen_block(os, wildcard->body, cin + "    ");
                if (block_falls_through(wildcard->body)) os << cin << "    break;\n";
                os << cin << "}\n";
            }
            os << in << "}\n";
            os << indent << "}\n";
            return;
        }

        os << in << "auto " << var << " = " << gen_expr(*m.subject) << ";\n";
        const MatchCase* wildcard = nullptr;
        bool first = true;
        for (const MatchCase& c : m.cases) {
            if (c.wildcard) {
                wildcard = &c;  // last `_` wins; emitted as the trailing else
                continue;
            }
            os << in << (first ? "if (" : "else if (") << var << " == " << gen_expr(*c.pattern)
               << ") {\n";
            gen_block(os, c.body, in + "    ");
            os << in << "}\n";
            first = false;
        }
        if (wildcard != nullptr) {
            os << in << (first ? "{\n" : "else {\n");
            gen_block(os, wildcard->body, in + "    ");
            os << in << "}\n";
        }
        os << indent << "}\n";
    }

    void gen_for(std::ostringstream& os, const For& f, const std::string& indent) {
        // Only `for x in range(stop)` / `range(start, stop)` for now (no containers yet).
        if (f.iterable->kind == ExprKind::Call) {
            const auto* call = static_cast<const Call*>(f.iterable.get());
            const Ident* id = call->callee->kind == ExprKind::Ident
                                  ? static_cast<const Ident*>(call->callee.get())
                                  : nullptr;
            if (id != nullptr && id->name == "range") {
                std::string start = "0";
                std::string stop;
                if (call->args.size() == 1) {
                    stop = gen_expr(*call->args[0]);
                } else if (call->args.size() == 2) {
                    start = gen_expr(*call->args[0]);
                    stop = gen_expr(*call->args[1]);
                } else {
                    diags_.emplace_back("codegen: range() takes 1 or 2 arguments");
                    return;
                }
                os << indent << "for (long long " << cpp_ident(f.var) << " = " << start << "; " << cpp_ident(f.var)
                   << " < " << stop << "; ++" << cpp_ident(f.var) << ") {\n";
                gen_block(os, f.body, indent + "    ");
                os << indent << "}\n";
                return;
            }
        }
        // General container iteration: for x in <list/array/…> -> range-based for.
        os << indent << "for (auto& " << cpp_ident(f.var) << " : " << gen_expr(*f.iterable) << ") {\n";
        gen_block(os, f.body, indent + "    ");
        os << indent << "}\n";
    }

    std::string module_namespace(const std::vector<std::string>& segs) const {
        // When the module has an alias (`namespace io = ::cheatah::io;`), drop the
        // prefix — `io::path::join` instead of `::cheatah::io::path::join`. Otherwise
        // stay explicit, global-qualified so it can't bind to anything in the program
        // namespace.
        std::string ns;
        std::size_t start = 0;
        if (!segs.empty() && aliased_roots_.contains(segs[0])) {
            ns = cpp_ident(segs[0]);
            start = 1;
        } else {
            ns = "::cheatah";
        }
        for (std::size_t i = start; i < segs.size(); ++i) ns += "::" + cpp_ident(segs[i]);
        return ns;
    }

    std::optional<std::vector<std::string>> resolve_module_path(const Expr& e) const {
        if (e.kind == ExprKind::Ident) {
            const auto it = aliases_.find(static_cast<const Ident&>(e).name);
            if (it != aliases_.end()) return it->second;
            return std::nullopt;
        }
        if (e.kind == ExprKind::Member) {
            const auto& m = static_cast<const Member&>(e);
            if (auto base = resolve_module_path(*m.object)) {
                base->push_back(m.name);
                return base;
            }
        }
        return std::nullopt;
    }

    // Whether @p e is STATICALLY a std::string, so `+` is string concatenation and a
    // surrounding str() is redundant: a string literal, a bare `str(...)` call, or a `+`
    // with a stringy operand. Used to insert str() only where it is actually needed and to
    // collapse `str(str(x))` — keeping the emitted C++ minimal and close to the source.
    bool is_stringy(const Expr& e) const {
        switch (e.kind) {
            case ExprKind::StringLit:
                return true;
            case ExprKind::Call: {
                const auto& c = static_cast<const Call&>(e);
                return c.callee->kind == ExprKind::Ident &&
                       static_cast<const Ident&>(*c.callee).name == "str" &&
                       !defined_names_.contains("str") && c.args.size() == 1;
            }
            case ExprKind::Binary: {
                const auto& b = static_cast<const Binary&>(e);
                return b.op == "+" && (is_stringy(*b.lhs) || is_stringy(*b.rhs));
            }
            default:
                return false;
        }
    }

    // @p indent is the leading whitespace of the enclosing statement; a multi-line list/
    // dict literal uses it so the generated C++ keeps the source's line structure aligned
    // under the statement (readable .gen.cpp). Scalar expressions ignore it.
    // @p expected_cpp_type, when non-null, is the C++ type a container LITERAL is being
    // constructed AS at a known-type site (a width-typed `let`/return/struct field). It is
    // consulted ONLY by the ListLit/DictLit cases, so the declared narrow element type wins over
    // CTAD (which would deduce vector<long long> and fail to assign). Everywhere else it is inert.
    std::string gen_expr(const Expr& e, const std::string& indent = "",
                         const std::string* expected_cpp_type = nullptr) {
        switch (e.kind) {
            case ExprKind::StringLit:
                // Strings are first-class std::string: enables `"a" + "b"` and
                // string containers, and converts freely to string_view.
                return "std::string(" + cpp_string_literal(static_cast<const StringLit&>(e).value) +
                       ")";
            case ExprKind::NumberLit: {
                // cheatah `int` is 64-bit; integer literals -> long long (so a
                // list[int] literal deduces vector<long long>). Floats stay double.
                const auto& n = static_cast<const NumberLit&>(e);
                const bool is_float = n.text.find('.') != std::string::npos ||
                                      n.text.find('e') != std::string::npos ||
                                      n.text.find('E') != std::string::npos;
                return is_float ? n.text : n.text + "LL";
            }
            case ExprKind::BoolLit:
                return static_cast<const BoolLit&>(e).value ? "true" : "false";
            case ExprKind::Ident: {
                const auto& name = static_cast<const Ident&>(e).name;
                if (in_method_ && name == "self") return "(*this)";  // method receiver
                // A bare standalone identifier is a module reference only when it is NOT
                // shadowed by a locally-defined name (a var/param/fn/struct/…). A module
                // name on its own can't be a value, so a same-named local always wins —
                // emit it bare. (Member access `math.sqrt` resolves separately.)
                if (!defined_names_.contains(name)) {
                    const auto it = aliases_.find(name);
                    if (it != aliases_.end()) return module_namespace(it->second);
                }
                return cpp_ident(name);
            }
            case ExprKind::Member: {
                const auto& m = static_cast<const Member&>(e);
                // A scoped enum member: EnumName.MEMBER -> EnumName::MEMBER.
                if (m.object->kind == ExprKind::Ident &&
                    enum_names_.contains(static_cast<const Ident&>(*m.object).name)) {
                    return static_cast<const Ident&>(*m.object).name + "::" + cpp_ident(m.name);
                }
                if (auto path = resolve_module_path(m)) return module_namespace(*path);
                return gen_expr(*m.object) + "." + cpp_ident(m.name);
            }
            case ExprKind::Index: {
                // Value position: negative-aware, and string indexing yields a
                // length-1 string. Assignment targets use gen_lvalue. A multi-index
                // subscript (x[i, j, ...]) passes every index to builtins::index —
                // the ndarray module provides those overloads.
                const auto& ix = static_cast<const Index&>(e);
                std::string out = builtins_ns_ + "index(" + gen_expr(*ix.object) + ", " +
                                  gen_expr(*ix.index);
                for (const ExprPtr& more : ix.extra) out += ", " + gen_expr(*more);
                return out + ")";
            }
            case ExprKind::Slice: {
                const auto& sl = static_cast<const Slice&>(e);
                const std::string lo = sl.start ? gen_expr(*sl.start) : "0LL";
                const std::string hi =
                    sl.stop ? gen_expr(*sl.stop) : builtins_ns_ + "slice_end";
                return builtins_ns_ + "slice(" + gen_expr(*sl.object) + ", " + lo + ", " + hi +
                       ")";
            }
            case ExprKind::Unary: {
                const auto& u = static_cast<const Unary&>(e);
                return "(" + u.op + gen_expr(*u.operand) + ")";
            }
            case ExprKind::Binary: {
                const auto& b = static_cast<const Binary&>(e);
                const std::string L = gen_expr(*b.lhs, indent), R = gen_expr(*b.rhs, indent);
                if (b.op == "**")  // no C++ infix power -> std::pow
                    return "std::pow(" + L + ", " + R + ")";
                // `/` is TRUE division (always float, like Python 3); `//` is FLOOR
                // division (opt-in). Both go through builtins helpers so integer
                // operands don't silently truncate the way raw C++ `/` would.
                if (b.op == "/") return builtins_ns_ + "truediv(" + L + ", " + R + ")";
                if (b.op == "//") return builtins_ns_ + "floordiv(" + L + ", " + R + ")";
                // `%` is Python FLOOR-mod (result takes the divisor's sign); `in` is the
                // membership test (string substring, list element, dict key) — both lower
                // to builtins helpers so the semantics match Python, not raw C++.
                if (b.op == "%") return builtins_ns_ + "mod(" + L + ", " + R + ")";
                if (b.op == "in") return builtins_ns_ + "contains(" + R + ", " + L + ")";
                // String concatenation with a non-string operand: stringify the other side
                // so `"n=" + a` works and emits the SAME minimal C++ as the explicit
                // `"n=" + str(a)` — only inserting str() where it is actually needed. When
                // both sides (or neither) are already strings, emit the `+` verbatim, so the
                // generated C++ stays as close to the source as possible.
                if (b.op == "+") {
                    const bool ls = is_stringy(*b.lhs), rs = is_stringy(*b.rhs);
                    if (ls && !rs) return "(" + L + " + " + builtins_ns_ + "str(" + R + "))";
                    if (rs && !ls) return "(" + builtins_ns_ + "str(" + L + ") + " + R + ")";
                }
                return "(" + L + " " + b.op + " " + R + ")";
            }
            case ExprKind::ListLit: {
                const auto& lst = static_cast<const ListLit&>(e);
                // A declared width-typed element (`let v: list[i8] = [1,2,3]`) must WIN over CTAD.
                // Construct the literal AS the declared vector type: constant literals narrow
                // cleanly ([dcl.init.list]'s constant-expression rule), and an out-of-range literal
                // (300 into i8) becomes a COMPILE ERROR — a free, zero-cost bounds check.
                const bool typed = (expected_cpp_type != nullptr) && !expected_cpp_type->empty();
                const std::string ctor = typed ? *expected_cpp_type : "std::vector";
                if (lst.elements.empty()) {
                    if (typed) return ctor + "{}";
                    diags_.emplace_back("codegen: empty list literal needs a type annotation");
                    return "std::vector<long long>{}";
                }
                // A source list that spanned multiple lines stays multi-line, with each
                // element on its own line indented under the literal — so a big nested
                // array is readable in .gen.cpp instead of one unwieldy line.
                if (lst.multiline) {
                    const std::string inner = indent + "    ";
                    std::string out = ctor + "{\n";
                    for (std::size_t i = 0; i < lst.elements.size(); ++i) {
                        out += inner + gen_expr(*lst.elements[i], inner);
                        if (i + 1 < lst.elements.size()) out += ",";
                        out += "\n";
                    }
                    return out + indent + "}";
                }
                return ctor + "{" + gen_args(lst.elements) + "}";  // typed -> declared vector; else CTAD
            }
            case ExprKind::DictLit: {
                const auto& d = static_cast<const DictLit&>(e);
                // As ListLit: a declared width-typed key/value drives the map type. Typed entries are
                // plain braced pairs `{k, v}` (so a constant value narrows into the declared type);
                // untyped entries keep `std::pair{…}` + CTAD.
                const bool typed = (expected_cpp_type != nullptr) && !expected_cpp_type->empty();
                auto entry = [&](std::size_t i, const std::string& ind) {
                    return typed ? "{" + gen_expr(*d.keys[i], ind) + ", " + gen_expr(*d.values[i], ind) + "}"
                                 : "std::pair{" + gen_expr(*d.keys[i], ind) + ", " + gen_expr(*d.values[i], ind) + "}";
                };
                const std::string ctor = typed ? *expected_cpp_type : "std::unordered_map";
                if (d.keys.empty()) {
                    if (typed) return ctor + "{}";
                    diags_.emplace_back("codegen: empty dict literal needs a type annotation");
                    return "std::unordered_map<long long, long long>{}";
                }
                if (d.multiline) {
                    const std::string inner = indent + "    ";
                    std::string out = ctor + "{\n";
                    for (std::size_t i = 0; i < d.keys.size(); ++i) {
                        out += inner + entry(i, inner);
                        if (i + 1 < d.keys.size()) out += ",";
                        out += "\n";
                    }
                    return out + indent + "}";
                }
                std::string out = ctor + "{";
                for (std::size_t i = 0; i < d.keys.size(); ++i) {
                    out += (i != 0 ? ", " : "");
                    out += entry(i, "");
                }
                return out + "}";
            }
            case ExprKind::StructInit: {
                // Reached only OUTSIDE a struct call (the `Type({…})` form is handled in the
                // Call case with field validation). A bare designated initializer can't be
                // type-checked here, so emit the braces and let the C++ backend resolve it.
                const auto& si = static_cast<const StructInit&>(e);
                std::string out = "{";
                for (std::size_t i = 0; i < si.fields.size(); ++i) {
                    out += (i == 0 ? "" : ", ");
                    out += "." + cpp_ident(si.fields[i]) + " = " + gen_expr(*si.values[i], indent);
                }
                return out + "}";
            }
            case ExprKind::Call: {
                const auto& c = static_cast<const Call&>(e);
                bool has_kwargs = false;
                for (const std::string& n : c.arg_names) {
                    if (!n.empty()) has_kwargs = true;
                }
                if (has_kwargs) {
                    return gen_kwargs_call(c, indent);  // reorder against the known signature
                }
                if (c.callee->kind == ExprKind::Ident) {
                    const auto& id = static_cast<const Ident&>(*c.callee);
                    if (struct_names_.contains(id.name)) {
                        // `Type({.f = v, …})` -> a C++20 designated initializer; fields not
                        // listed default-initialize (never garbage — an unset value is a bug).
                        if (c.args.size() == 1 && c.args[0]->kind == ExprKind::StructInit) {
                            return gen_struct_init(id.name,
                                                   static_cast<const StructInit&>(*c.args[0]), indent);
                        }
                        return id.name + "{" + gen_args(c.args, false, indent) + "}";  // positional
                    }
                    // str() of something already a string is identity — drop it. So
                    // `str(str(a))` reduces to `str(a)` and `str("x")` to `"x"`: minimal C++,
                    // no redundant intermediary.
                    if (id.name == "str" && !defined_names_.contains("str") && c.args.size() == 1 &&
                        is_stringy(*c.args[0])) {
                        return gen_expr(*c.args[0], indent);
                    }
                    // `sizeof(f32)`, `sizeof(int)`, `sizeof(expr)` — the compile-time size
                    // builtin, lowered to a real C++ sizeof (see sizeof_type_spelling above).
                    // A user-defined `sizeof` function shadows it, like `str`.
                    if (id.name == "sizeof" && !defined_names_.contains("sizeof") &&
                        c.args.size() == 1) {
                        if (c.args[0]->kind == ExprKind::Ident) {
                            const auto& t = static_cast<const Ident&>(*c.args[0]);
                            if (auto s = sizeof_type_spelling(t.name)) return "sizeof(" + *s + ")";
                        }
                        return "sizeof(" + gen_expr(*c.args[0], indent) + ")";
                    }
                    if (auto bi = builtin_cpp_name(id.name)) {
                        return builtins_ns_ + *bi + "(" + gen_args(c.args, false, indent) + ")";
                    }
                }
                // Method-call syntax via UFCS for cheatah's value-methods: a call
                // `obj.append(x)` on a non-module value lowers to
                // `builtins::append(obj, x)`. Restricted to a known set so
                // genuine member methods on module classes (io File.write/close,
                // ndarray NDArray.…) stay as direct member calls, and module calls
                // (io.print, os.path.join) keep their namespace qualification.
                if (c.callee->kind == ExprKind::Member) {
                    const auto& m = static_cast<const Member&>(*c.callee);
                    if (is_builtin_method(m.name) && !resolve_module_path(m)) {
                        std::string out = builtins_ns_ + m.name + "(" + gen_expr(*m.object);
                        for (const ExprPtr& a : c.args) out += ", " + gen_expr(*a);
                        return out + ")";
                    }
                    // `arr.astype(i16)` — ndarray element conversion (numpy's a.astype(dtype)). The
                    // sole argument is a TYPE NAME (a width like `i16`, or `int`/`float`), resolved
                    // exactly as sizeof's, and lowered to the free `ndarray::astype<U>(arr)` (a free
                    // function, so no `.template` disambiguation is ever needed). Building a narrow
                    // ndarray this way is how the sized-int footprint win reaches ndarray.
                    if (m.name == "astype" && !resolve_module_path(m) && c.args.size() == 1 &&
                        c.args[0]->kind == ExprKind::Ident) {
                        const auto& t = static_cast<const Ident&>(*c.args[0]);
                        std::optional<std::string> u = width_cpp_type(t.name);
                        if (!u && t.name == "int") u = "long long";
                        if (!u && t.name == "float") u = "double";
                        if (u)
                            return "::cheatah::ndarray::astype<" + *u + ">(" + gen_expr(*m.object) + ")";
                    }
                }
                const std::string callee = gen_expr(*c.callee);
                // Explicit template args (`Store<float, 1024>(…)`): map each cheatah type (or
                // non-type literal) to its C++ spelling and splice `<...>` between the callee and
                // its argument list.
                std::string targs;
                if (!c.type_args.empty()) {
                    targs = "<";
                    for (std::size_t i = 0; i < c.type_args.size(); ++i) {
                        targs += (i != 0 ? ", " : "");
                        targs += map_type_arg(c.type_args[i]);
                    }
                    targs += ">";
                }
                // `io.print` streams every argument and `io.format` takes its format as
                // a std::string_view — both accept a string literal as a bare const char*
                // with no intermediate std::string. Detect them by resolved module path
                // so it works whether or not `io` got a namespace alias.
                bool bare = false;
                if (auto p = resolve_module_path(*c.callee)) {
                    bare = (*p == std::vector<std::string>{"io", "print"} ||
                            *p == std::vector<std::string>{"io", "format"});
                }
                return callee + targs + "(" + gen_args(c.args, bare, indent) + ")";
            }
        }
        diags_.emplace_back("codegen: unsupported expression node");
        return "/* unsupported */";
    }

    std::string gen_args(const std::vector<ExprPtr>& args, bool bare_string_literals = false,
                         const std::string& indent = "") {
        std::string out;
        for (std::size_t i = 0; i < args.size(); ++i) {
            out += (i != 0 ? ", " : "");
            // For callees that only STREAM their arguments (io.print) or take a
            // std::string_view (io.format's format string), a string literal passes as
            // a bare `const char*` — no throwaway std::string built just to print it.
            if (bare_string_literals && args[i]->kind == ExprKind::StringLit) {
                out += cpp_string_literal(static_cast<const StringLit&>(*args[i]).value);
            } else if (args[i]->kind == ExprKind::Ident &&
                       fn_defs_.contains(static_cast<const Ident&>(*args[i]).name)) {
                // A bare user-function NAME passed as a VALUE (a callback). cheatah `fn`s
                // emit as abbreviated-template overload sets with no single address, so a
                // bare name cannot bind to a `std::function<...>` parameter (C++ reports an
                // "overloaded function type"). Wrap it in a capture-less generic forwarding
                // lambda — a single callable object that `std::function` instantiates for
                // whatever concrete signature the receiving side declares. This makes rich
                // types (int/float/bool/str/ndarray/struct/list/dict, passed AND returned)
                // flow through automatically, and the empty capture makes it safe to store
                // and invoke from another thread (e.g. a brain's decision thread).
                const std::string& fname = static_cast<const Ident&>(*args[i]).name;
                const FnDef& fd = *fn_defs_.at(fname);
                if (fn_fully_typed(fd)) {
                    // A FULLY-TYPED fn -> a capture-less lambda with its DECLARED concrete
                    // signature, made a function POINTER via unary `+` so the signature is
                    // readable for CTAD (e.g. a brain deducing its trigger type from a
                    // callback) while still binding to any matching std::function.
                    std::string params, fwd;
                    for (std::size_t k = 0; k < fd.params.size(); ++k) {
                        params += (k != 0 ? ", " : "");
                        params += lambda_param_cpp(fd.param_types[k]) + " " + fd.params[k];
                        fwd += (k != 0 ? ", " : "") + fd.params[k];
                    }
                    out += "+[](";
                    out += params;
                    out += ") -> ";
                    out += return_type_cpp(fd.return_type);
                    out += " { return ";
                    out += fname;
                    out += "(";
                    out += fwd;
                    out += "); }";
                } else {
                    out += "[](auto&&... _a){ return " + fname + "(_a...); }";
                }
            } else {
                out += gen_expr(*args[i], indent);
            }
        }
        return out;
    }

    // Every identifier the PROGRAM introduces (let/assign targets, loop & catch vars,
    // function/struct/enum/interface names, parameters). A module is given a `namespace`
    // alias only when its name is NOT in here, so the alias can never shadow user code.
    std::unordered_set<std::string> defined_names_;
    // `let x` declarations with NO initializer, not yet realized: emitted (as `auto x = …`)
    // at the first assignment to x; an entry that is never realized was never given a value.
    std::unordered_set<std::string> deferred_lets_;
    // The C++ type a `return <container literal>` should be constructed AS, but ONLY when the
    // enclosing function's `-> Type` hint uses an explicit width (`-> list[i8]`); empty otherwise,
    // so a plain `-> list[int]` return keeps its CTAD spelling (no churn). Set around each function
    // body with a declared return type (gen_fn / gen_fn_library; methods return `auto`).
    std::string return_type_hint_;
    // Names bound by `constexpr let` in the body being emitted — i.e. compile-time constants.
    // `if`/`match` whose condition/subject is built only from these (plus literals and the
    // safe constant operators) auto-lower to their `if constexpr` form. Scoped per body
    // (cleared wherever deferred_lets_ is), so a constant in one function never leaks names
    // into another where the same identifier is a runtime value.
    std::unordered_set<std::string> const_vars_;
    // Imported-module roots that are safe to alias (root ∉ defined_names_).
    std::unordered_set<std::string> aliased_roots_;
    // From-imported symbols: (bare name, full module path incl. the symbol). Drives the `using`
    // declarations in emit_aliases; the parallel aliases_ entries drive `Sym.MEMBER` resolution.
    std::vector<std::pair<std::string, std::vector<std::string>>> from_imports_;
    // The namespace prefix for built-in calls: the short alias `builtins::` normally,
    // or the explicit `cheatah::builtins::` if the program itself defines `builtins`.
    std::string builtins_ns_ = "builtins::";

    // Walk the whole program collecting names it introduces (recurses into every block).
    void collect_defined(const Block& body) {
        for (const StmtPtr& s : body) collect_defined_stmt(*s);
    }
    void collect_defined_stmt(const Stmt& s) {
        switch (s.kind) {
            case StmtKind::Let:
                defined_names_.insert(static_cast<const Let&>(s).name);
                break;
            case StmtKind::Assign: {
                const auto& a = static_cast<const Assign&>(s);
                if (a.target->kind == ExprKind::Ident)
                    defined_names_.insert(static_cast<const Ident&>(*a.target).name);
                break;
            }
            case StmtKind::For: {
                const auto& f = static_cast<const For&>(s);
                defined_names_.insert(f.var);
                collect_defined(f.body);
                break;
            }
            case StmtKind::Try: {
                const auto& t = static_cast<const Try&>(s);
                collect_defined(t.body);
                for (const Handler& h : t.handlers) {
                    if (!h.var.empty()) defined_names_.insert(h.var);
                    collect_defined(h.body);
                }
                if (t.has_finally) collect_defined(t.finally_body);
                break;
            }
            case StmtKind::If: {
                const auto& i = static_cast<const If&>(s);
                collect_defined(i.then_body);
                collect_defined(i.else_body);
                break;
            }
            case StmtKind::While:
                collect_defined(static_cast<const While&>(s).body);
                break;
            case StmtKind::With: {
                const auto& w = static_cast<const With&>(s);
                if (!w.bind.empty()) defined_names_.insert(w.bind);
                collect_defined(w.body);
                break;
            }
            case StmtKind::Match:
                for (const auto& c : static_cast<const Match&>(s).cases) collect_defined(c.body);
                break;
            case StmtKind::FnDef: {
                const auto& fn = static_cast<const FnDef&>(s);
                defined_names_.insert(fn.name);
                for (const std::string& p : fn.params) defined_names_.insert(p);
                collect_defined(fn.body);
                break;
            }
            case StmtKind::StructDef: {
                const auto& sd = static_cast<const StructDef&>(s);
                defined_names_.insert(sd.name);
                for (const StmtPtr& m : sd.methods) collect_defined_stmt(*m);  // FnDefs
                break;
            }
            case StmtKind::EnumDef:
                defined_names_.insert(static_cast<const EnumDef&>(s).name);
                break;
            case StmtKind::InterfaceDef:
                defined_names_.insert(static_cast<const InterfaceDef&>(s).name);
                break;
            default:
                break;  // Import/ExprStmt/Return/Raise/RawCpp/Break/Continue: no new names
        }
    }

    std::unordered_map<std::string, std::vector<std::string>> aliases_;
    std::unordered_map<std::string, const FnDef*> fn_defs_;  // program functions, for kwargs/defaults
    std::set<std::string> roots_;
    std::unordered_set<std::string> struct_names_;
    std::unordered_map<std::string, std::vector<Field>> struct_fields_;  // struct name -> its fields
    std::unordered_set<std::string> interface_names_;
    std::unordered_set<std::string> enum_names_;
    std::vector<std::string> diags_;
    int match_id_ = 0;     // unique suffix for the temp in each lowered `match`
    int with_id_ = 0;      // unique suffix for the hidden local of an `as`-less `with`
    bool in_method_ = false;  // inside a struct method body, so `self` -> `(*this)`
    std::string source_file_;        // .purr path for #line directives ("" = none)
    bool line_directives_ = false;   // emit #line (set when source_file_ is non-empty)
    bool remove_unused_ = true;      // drop `let`s whose variable is never read (opt-out flag)
    bool emit_docs_ = false;         // re-emit .purr doc comments as Javadoc (library mode)
};

} // namespace

CodegenResult codegen(const Program& program, const std::string& source_file, bool remove_unused,
                      const std::string& base_hoist, const std::string& base_body) {
    Codegen c;
    c.set_source_file(source_file);
    c.set_remove_unused(remove_unused);
    return c.run(program, base_hoist, base_body);
}

CodegenResult codegen_library(const Program& program, const LibOptions& opts) {
    Codegen c;
    c.set_remove_unused(opts.remove_unused);
    return c.run_library(program, opts);
}

} // namespace cheatah
