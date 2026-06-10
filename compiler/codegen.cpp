#include "codegen.hpp"

#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace cheatah {

namespace {

// Every cheatah fn/method param lowers to a C++20 abbreviated function template.
// We constrain each with the baseline `Value` concept so no generated template is
// unconstrained — keeps the C++ compiler's errors comprehensible and gives the
// future purrc diagnostics a concept-failure hook. (See constrain-all-templates.)
constexpr const char* kParamConcept = "cheatah::builtins::Value auto ";

// Bare names that are Python built-ins (always available, no import) -> their C++
// symbol in cheatah::builtins. Keyword-named conversions map specially.
std::optional<std::string> builtin_cpp_name(const std::string& name) {
    static const std::map<std::string, std::string> kBuiltins = {
        {"len", "len"},   {"ord", "ord"},   {"chr", "chr"},     {"hex", "hex"},
        {"oct", "oct"},   {"bin", "bin"},   {"ascii", "ascii"}, {"hash", "hash"},
        {"bool", "to_bool"}, {"int", "to_int"}, {"float", "to_float"},
        {"append", "append"}, {"startswith", "startswith"},
        {"endswith", "endswith"}, {"contains", "contains"},
    };
    const auto it = kBuiltins.find(name);
    if (it == kBuiltins.end()) return std::nullopt;
    return "cheatah::builtins::" + it->second;
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
        case StmtKind::Try: {
            const auto& t = static_cast<const Try&>(s);
            return block_mutates_self(t.body) || block_mutates_self(t.catch_body);
        }
        case StmtKind::Match: {
            const auto& m = static_cast<const Match&>(s);
            for (const MatchCase& c : m.cases)
                if (block_mutates_self(c.body)) return true;
            return false;
        }
        default:
            return false;
    }
}
bool block_mutates_self(const Block& body) {
    for (const StmtPtr& s : body)
        if (stmt_mutates_self(*s)) return true;
    return false;
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
            for (const ExprPtr& a : c.args)
                if (refers_to(*a, name)) return true;
            return false;
        }
        case ExprKind::Unary:
            return refers_to(*static_cast<const Unary&>(e).operand, name);
        case ExprKind::Binary: {
            const auto& b = static_cast<const Binary&>(e);
            return refers_to(*b.lhs, name) || refers_to(*b.rhs, name);
        }
        case ExprKind::ListLit: {
            for (const ExprPtr& el : static_cast<const ListLit&>(e).elements)
                if (refers_to(*el, name)) return true;
            return false;
        }
        case ExprKind::DictLit: {
            const auto& d = static_cast<const DictLit&>(e);
            for (const ExprPtr& k : d.keys)
                if (refers_to(*k, name)) return true;
            for (const ExprPtr& v : d.values)
                if (refers_to(*v, name)) return true;
            return false;
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
    if (t.name == "list" && t.args.size() == 1) {
        return "std::vector<" + map_type(t.args[0]) + ">";
    }
    if (t.name == "dict" && t.args.size() == 2) {
        return "std::unordered_map<" + map_type(t.args[0]) + ", " + map_type(t.args[1]) + ">";
    }
    if (t.name == "array" && t.args.size() == 1 && !t.array_size.empty()) {
        return "std::array<" + map_type(t.args[0]) + ", " + t.array_size + ">";
    }
    return t.name;  // a struct name
}

class Codegen {
public:
    CodegenResult run(const Program& prog) {
        // Pass 1: imports (modules to link) + struct names (for construction syntax).
        for (const StmtPtr& s : prog.body) {
            if (s->kind == StmtKind::Import) {
                const auto& imp = static_cast<const Import&>(*s);
                const std::string key = imp.alias.empty() ? imp.module.front() : imp.alias;
                aliases_[key] = imp.module;
                roots_.insert(imp.module.front());
            } else if (s->kind == StmtKind::StructDef) {
                struct_names_.insert(static_cast<const StructDef&>(*s).name);
            } else if (s->kind == StmtKind::InterfaceDef) {
                interface_names_.insert(static_cast<const InterfaceDef&>(*s).name);
            } else if (s->kind == StmtKind::EnumDef) {
                enum_names_.insert(static_cast<const EnumDef&>(*s).name);
            }
        }

        std::ostringstream os;
        os << "// Generated by purrc — do not edit.\n";
        os << "#include <array>\n#include <cmath>\n#include <concepts>\n#include <memory>\n"
              "#include <stdexcept>\n#include <string>\n#include <unordered_map>\n"
              "#include <utility>\n#include <vector>\n";
        // Enums generate an operator<< (their debug text form) using std::ostream, so
        // pull it in even when the program does not `import io`.
        if (!enum_names_.empty()) os << "#include <ostream>\n";
        os << "#include \"builtins.hpp\"\n";  // built-ins are always available
        for (const std::string& root : roots_) {
            os << "#include \"" << root << ".hpp\"\n";
        }
        // purr_main is the symbol the runtime resolves; on Windows a DLL only exposes
        // dllexport'd symbols, so wrap the linkage in a portable macro (no-op elsewhere).
        os << "#if defined(_WIN32)\n"
              "#define PURR_EXPORT extern \"C\" __declspec(dllexport)\n"
              "#else\n"
              "#define PURR_EXPORT extern \"C\"\n"
              "#endif\n";
        os << "\n";

        // Raw C++ escape hatch: top-level `cpp { … }` blocks are emitted at FILE
        // SCOPE (before structs/functions), so they can carry #includes, helper
        // functions, and types the rest of the program can use.
        for (const StmtPtr& s : prog.body) {
            if (s->kind == StmtKind::RawCpp) {
                os << static_cast<const RawCpp&>(*s).code << "\n";
            }
        }

        // Enums (scoped `enum class`) first — struct fields, function params, and
        // expressions may all refer to an enum type or its members.
        for (const StmtPtr& s : prog.body) {
            if (s->kind == StmtKind::EnumDef) gen_enum(os, static_cast<const EnumDef&>(*s));
        }

        // Interfaces (concepts) next — structs static_assert against them and
        // functions constrain parameters by them.
        for (const StmtPtr& s : prog.body) {
            if (s->kind == StmtKind::InterfaceDef)
                gen_interface(os, static_cast<const InterfaceDef&>(*s));
        }

        // Struct definitions, then function definitions, at file scope.
        for (const StmtPtr& s : prog.body) {
            if (s->kind == StmtKind::StructDef) gen_struct(os, static_cast<const StructDef&>(*s));
        }
        for (const StmtPtr& s : prog.body) {
            if (s->kind == StmtKind::FnDef) gen_fn(os, static_cast<const FnDef&>(*s));
        }

        // The runtime resolves this symbol and calls it.
        os << "PURR_EXPORT void purr_main() {\n";
        for (const StmtPtr& s : prog.body) {
            if (s->kind == StmtKind::Import || s->kind == StmtKind::StructDef ||
                s->kind == StmtKind::FnDef || s->kind == StmtKind::RawCpp ||
                s->kind == StmtKind::InterfaceDef || s->kind == StmtKind::EnumDef) {
                continue;  // emitted above (imports -> includes; top-level cpp -> file scope)
            }
            gen_stmt(os, *s, "    ");
        }
        os << "}\n";

        CodegenResult r;
        r.source = os.str();
        for (const std::string& root : roots_) r.modules.push_back(root);
        r.diagnostics = std::move(diags_);
        return r;
    }

private:
    // The constraint prefix for a parameter declared with type @p type_name: an
    // interface name -> that concept (`Shape auto`); otherwise the baseline Value.
    std::string param_prefix(const std::string& type_name) const {
        if (!type_name.empty() && interface_names_.count(type_name)) {
            return type_name + " auto ";
        }
        return kParamConcept;
    }

    // interface -> a C++20 concept: every method must be callable on the type, so a
    // `struct S : Iface` whose methods don't match fails the static_assert below.
    // enum Name { A [= v], … } -> a scoped, type-safe `enum class`. The underlying
    // type is left implicit (int), as for a plain C++ `enum class`. An explicit value
    // is emitted as a C++ constant expression (so `A = 1`, or `B = A + 1` referring to
    // an earlier member, both work). Members are reached scoped (Name::A) — see the
    // Member case in gen_expr.
    void gen_enum(std::ostringstream& os, const EnumDef& ed) {
        os << "enum class " << ed.name << " {\n";
        for (const Enumerator& en : ed.enumerators) {
            os << "    " << en.name;
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
            os << "    if (v_ == " << ed.name << "::" << en.name << ") return os_ << \""
               << ed.name << "." << en.name << "\";\n";
        }
        os << "    return os_ << \"" << ed.name
           << "(\" << static_cast<long long>(v_) << \")\";\n";
        os << "}\n\n";
    }

    void gen_interface(std::ostringstream& os, const InterfaceDef& id) {
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

    void gen_struct(std::ostringstream& os, const StructDef& sd) {
        os << "struct " << sd.name << " {\n";
        for (const Field& f : sd.fields) {
            os << "    " << map_type(f.type) << " " << f.name << ";\n";
        }
        // Methods become member functions. A leading `self` param is implicit (it
        // is `*this`); the struct stays a C++ aggregate (member functions are
        // allowed), so `Name{...}` construction is unaffected.
        for (const StmtPtr& m : sd.methods) {
            gen_method(os, static_cast<const FnDef&>(*m));
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
        os << "\n";
    }

    // The type declared for a method param (parallel to params; the implicit `self`
    // at index 0 has none), or "" when untyped.
    std::string method_param_type(const FnDef& fd, std::size_t i) const {
        return i < fd.param_types.size() ? fd.param_types[i] : std::string();
    }

    void gen_method(std::ostringstream& os, const FnDef& fd) {
        const bool has_self = !fd.params.empty() && fd.params[0] == "self";
        os << "    auto " << fd.name << "(";
        bool first = true;
        for (std::size_t i = (has_self ? 1 : 0); i < fd.params.size(); ++i) {
            os << (first ? "" : ", ") << param_prefix(method_param_type(fd, i)) << fd.params[i];
            first = false;
        }
        // A method that never assigns through `self` is `const`, so it works on
        // const objects — required for the print/`str()` protocol and read-only use.
        const bool mutates = has_self && block_mutates_self(fd.body);
        os << ")" << (mutates ? "" : " const") << " {\n";
        const bool prev = in_method_;
        in_method_ = true;
        gen_block(os, fd.body, "        ");
        in_method_ = prev;
        os << "    }\n";
    }

    void gen_fn(std::ostringstream& os, const FnDef& fd) {
        // `static` -> internal linkage, so the optimizer inlines/optimizes these
        // like local C++ functions (no exported-symbol interposition barrier).
        // Untyped params -> C++20 abbreviated function template; an interface-typed
        // param becomes a concept-constrained `auto` (static dispatch).
        os << "static auto " << fd.name << "(";
        for (std::size_t i = 0; i < fd.params.size(); ++i) {
            os << (i != 0 ? ", " : "") << param_prefix(method_param_type(fd, i)) << fd.params[i];
        }
        os << ") {\n";
        gen_block(os, fd.body, "    ");
        os << "}\n\n";
    }

    void gen_block(std::ostringstream& os, const Block& block, const std::string& indent) {
        for (const StmtPtr& s : block) gen_stmt(os, *s, indent);
    }

    // An assignment target is an lvalue: `xs[i] = v` / `d[k] = v` must use a raw
    // subscript (not the by-value `index()` helper used in value position).
    std::string gen_lvalue(const Expr& e) {
        if (e.kind == ExprKind::Index) {
            const auto& ix = static_cast<const Index&>(e);
            return gen_expr(*ix.object) + "[" + gen_expr(*ix.index) + "]";
        }
        return gen_expr(e);
    }

    void gen_stmt(std::ostringstream& os, const Stmt& s, const std::string& indent) {
        switch (s.kind) {
            case StmtKind::Let: {
                const auto& l = static_cast<const Let&>(s);
                if (l.has_type) {
                    // An explicit type drives the declaration, so empty `[]` / `{}`
                    // get their element types from the annotation (not deduced).
                    const bool empty_list =
                        l.value->kind == ExprKind::ListLit &&
                        static_cast<const ListLit&>(*l.value).elements.empty();
                    const bool empty_dict =
                        l.value->kind == ExprKind::DictLit &&
                        static_cast<const DictLit&>(*l.value).keys.empty();
                    if (empty_list || empty_dict) {
                        os << indent << map_type(l.type) << " " << l.name << ";\n";
                    } else {
                        os << indent << map_type(l.type) << " " << l.name << " = "
                           << gen_expr(*l.value) << ";\n";
                    }
                } else {
                    os << indent << "auto " << l.name << " = " << gen_expr(*l.value) << ";\n";
                }
                return;
            }
            case StmtKind::Assign: {
                const auto& a = static_cast<const Assign&>(s);
                // Self-append fast path: `x = x + e1 + e2 …` -> `x += e1; x += e2; …`,
                // so building a string (or accumulator) doesn't copy the whole left
                // operand each step (O(n²) -> O(n); no full-length temporaries). Only
                // when the target is a plain variable that sits at the head of the `+`
                // chain and is not re-read by the appended operands.
                if (a.target->kind == ExprKind::Ident && a.value->kind == ExprKind::Binary &&
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
                            expr = (i == 1) ? name + " += " + operand
                                            : "(" + expr + ") += " + operand;
                        }
                        os << indent << expr << ";\n";
                        return;
                    }
                }
                os << indent << gen_lvalue(*a.target) << " = " << gen_expr(*a.value) << ";\n";
                return;
            }
            case StmtKind::If: {
                const auto& n = static_cast<const If&>(s);
                os << indent << "if (" << gen_expr(*n.cond) << ") {\n";
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
                os << indent << "while (" << gen_expr(*w.cond) << ") {\n";
                gen_block(os, w.body, indent + "    ");
                os << indent << "}\n";
                return;
            }
            case StmtKind::For:
                gen_for(os, static_cast<const For&>(s), indent);
                return;
            case StmtKind::Return: {
                const auto& r = static_cast<const Return&>(s);
                os << indent << "return" << (r.value ? " " + gen_expr(*r.value) : "") << ";\n";
                return;
            }
            case StmtKind::Try: {
                const auto& t = static_cast<const Try&>(s);
                const std::string exc = (t.catch_var.empty() ? "_purr" : t.catch_var) + "_exc";
                os << indent << "try {\n";
                gen_block(os, t.body, indent + "    ");
                os << indent << "} catch (const std::exception& " << exc << ") {\n";
                if (!t.catch_var.empty()) {
                    os << indent << "    auto " << t.catch_var << " = std::string(" << exc
                       << ".what());\n";
                }
                gen_block(os, t.catch_body, indent + "    ");
                os << indent << "}\n";
                return;
            }
            case StmtKind::Raise:
                os << indent << "throw std::runtime_error("
                   << gen_expr(*static_cast<const Raise&>(s).value) << ");\n";
                return;
            case StmtKind::ExprStmt:
                os << indent << gen_expr(*static_cast<const ExprStmt&>(s).expr) << ";\n";
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
                return;  // emitted at file scope
        }
    }

    // match <subject> { case v { … } case _ { … } } -> evaluate the subject once,
    // then an if / else-if chain comparing it with `==`; the `_` case is the else.
    void gen_match(std::ostringstream& os, const Match& m, const std::string& indent) {
        const std::string var = "__match_" + std::to_string(match_id_++);
        os << indent << "{\n";
        const std::string in = indent + "    ";
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
                    diags_.push_back("codegen: range() takes 1 or 2 arguments");
                    return;
                }
                os << indent << "for (long long " << f.var << " = " << start << "; " << f.var
                   << " < " << stop << "; ++" << f.var << ") {\n";
                gen_block(os, f.body, indent + "    ");
                os << indent << "}\n";
                return;
            }
        }
        // General container iteration: for x in <list/array/…> -> range-based for.
        os << indent << "for (auto& " << f.var << " : " << gen_expr(*f.iterable) << ") {\n";
        gen_block(os, f.body, indent + "    ");
        os << indent << "}\n";
    }

    std::string module_namespace(const std::vector<std::string>& segs) const {
        std::string ns = "cheatah";
        for (const std::string& s : segs) ns += "::" + s;
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

    std::string gen_expr(const Expr& e) {
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
                const auto it = aliases_.find(name);
                return (it != aliases_.end()) ? module_namespace(it->second) : name;
            }
            case ExprKind::Member: {
                const auto& m = static_cast<const Member&>(e);
                // A scoped enum member: EnumName.MEMBER -> EnumName::MEMBER.
                if (m.object->kind == ExprKind::Ident &&
                    enum_names_.count(static_cast<const Ident&>(*m.object).name)) {
                    return static_cast<const Ident&>(*m.object).name + "::" + m.name;
                }
                if (auto path = resolve_module_path(m)) return module_namespace(*path);
                return gen_expr(*m.object) + "." + m.name;
            }
            case ExprKind::Index: {
                // Value position: negative-aware, and string indexing yields a
                // length-1 string. Assignment targets use gen_lvalue (raw [] ).
                const auto& ix = static_cast<const Index&>(e);
                return "cheatah::builtins::index(" + gen_expr(*ix.object) + ", " +
                       gen_expr(*ix.index) + ")";
            }
            case ExprKind::Slice: {
                const auto& sl = static_cast<const Slice&>(e);
                const std::string lo = sl.start ? gen_expr(*sl.start) : "0LL";
                const std::string hi =
                    sl.stop ? gen_expr(*sl.stop) : "cheatah::builtins::slice_end";
                return "cheatah::builtins::slice(" + gen_expr(*sl.object) + ", " + lo + ", " + hi +
                       ")";
            }
            case ExprKind::Unary: {
                const auto& u = static_cast<const Unary&>(e);
                return "(" + u.op + gen_expr(*u.operand) + ")";
            }
            case ExprKind::Binary: {
                const auto& b = static_cast<const Binary&>(e);
                const std::string L = gen_expr(*b.lhs), R = gen_expr(*b.rhs);
                if (b.op == "**")  // no C++ infix power -> std::pow
                    return "std::pow(" + L + ", " + R + ")";
                // `/` is TRUE division (always float, like Python 3); `//` is FLOOR
                // division (opt-in). Both go through builtins helpers so integer
                // operands don't silently truncate the way raw C++ `/` would.
                if (b.op == "/") return "cheatah::builtins::truediv(" + L + ", " + R + ")";
                if (b.op == "//") return "cheatah::builtins::floordiv(" + L + ", " + R + ")";
                return "(" + L + " " + b.op + " " + R + ")";
            }
            case ExprKind::ListLit: {
                const auto& lst = static_cast<const ListLit&>(e);
                if (lst.elements.empty()) {
                    diags_.push_back("codegen: empty list literal needs a type annotation");
                    return "std::vector<long long>{}";
                }
                return "std::vector{" + gen_args(lst.elements) + "}";  // CTAD -> element type
            }
            case ExprKind::DictLit: {
                const auto& d = static_cast<const DictLit&>(e);
                if (d.keys.empty()) {
                    diags_.push_back("codegen: empty dict literal needs a type annotation");
                    return "std::unordered_map<long long, long long>{}";
                }
                std::string out = "std::unordered_map{";
                for (std::size_t i = 0; i < d.keys.size(); ++i) {
                    out += (i != 0 ? ", " : "");
                    out += "std::pair{" + gen_expr(*d.keys[i]) + ", " + gen_expr(*d.values[i]) + "}";
                }
                return out + "}";
            }
            case ExprKind::Call: {
                const auto& c = static_cast<const Call&>(e);
                if (c.callee->kind == ExprKind::Ident) {
                    const auto& id = static_cast<const Ident&>(*c.callee);
                    if (struct_names_.count(id.name)) {
                        return id.name + "{" + gen_args(c.args) + "}";  // aggregate construction
                    }
                    if (auto bi = builtin_cpp_name(id.name)) {
                        return *bi + "(" + gen_args(c.args) + ")";
                    }
                }
                // Method-call syntax via UFCS for cheatah's value-methods: a call
                // `obj.append(x)` on a non-module value lowers to
                // `cheatah::builtins::append(obj, x)`. Restricted to a known set so
                // genuine member methods on module classes (io File.write/close,
                // ndarray NDArray.…) stay as direct member calls, and module calls
                // (io.print, os.path.join) keep their namespace qualification.
                if (c.callee->kind == ExprKind::Member) {
                    const auto& m = static_cast<const Member&>(*c.callee);
                    if (is_builtin_method(m.name) && !resolve_module_path(m)) {
                        std::string out = "cheatah::builtins::" + m.name + "(" + gen_expr(*m.object);
                        for (const ExprPtr& a : c.args) out += ", " + gen_expr(*a);
                        return out + ")";
                    }
                }
                return gen_expr(*c.callee) + "(" + gen_args(c.args) + ")";
            }
        }
        diags_.push_back("codegen: unsupported expression node");
        return "/* unsupported */";
    }

    std::string gen_args(const std::vector<ExprPtr>& args) {
        std::string out;
        for (std::size_t i = 0; i < args.size(); ++i) {
            out += (i != 0 ? ", " : "") + gen_expr(*args[i]);
        }
        return out;
    }

    std::map<std::string, std::vector<std::string>> aliases_;
    std::set<std::string> roots_;
    std::set<std::string> struct_names_;
    std::set<std::string> interface_names_;
    std::set<std::string> enum_names_;
    std::vector<std::string> diags_;
    int match_id_ = 0;     // unique suffix for the temp in each lowered `match`
    bool in_method_ = false;  // inside a struct method body, so `self` -> `(*this)`
};

} // namespace

CodegenResult codegen(const Program& program) { return Codegen().run(program); }

} // namespace cheatah
