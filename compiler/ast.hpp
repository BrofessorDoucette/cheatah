#pragma once

#include <memory>
#include <string>
#include <vector>

// cheatah AST — the parser's output and the codegen's input. Each node carries
// a `kind` tag so the codegen dispatches with a switch + static_cast (no RTTI /
// dynamic_cast). Memory is owned through unique_ptr throughout (no raw owning
// pointers).
namespace cheatah {

// ---- Expressions ----
enum class ExprKind {
    StringLit, NumberLit, BoolLit, Ident, Member, Index, Slice, Call, Unary, Binary,
    ListLit, DictLit, StructInit,
};

struct Expr {
    ExprKind kind;
    explicit Expr(ExprKind k) : kind(k) {}
    virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct StringLit : Expr {
    std::string value;  // decoded value (no quotes)
    explicit StringLit(std::string v) : Expr(ExprKind::StringLit), value(std::move(v)) {}
};

struct NumberLit : Expr {
    std::string text;  // "3" -> int (long long), "3.14" -> double
    explicit NumberLit(std::string t) : Expr(ExprKind::NumberLit), text(std::move(t)) {}
};

struct BoolLit : Expr {
    bool value;
    explicit BoolLit(bool v) : Expr(ExprKind::BoolLit), value(v) {}
};

struct Ident : Expr {
    std::string name;
    explicit Ident(std::string n) : Expr(ExprKind::Ident), name(std::move(n)) {}
};

struct Member : Expr {  // object.name
    ExprPtr object;
    std::string name;
    Member(ExprPtr o, std::string n)
        : Expr(ExprKind::Member), object(std::move(o)), name(std::move(n)) {}
};

struct Index : Expr {  // object[index]
    ExprPtr object;
    ExprPtr index;
    Index(ExprPtr o, ExprPtr i) : Expr(ExprKind::Index), object(std::move(o)), index(std::move(i)) {}
};

struct Slice : Expr {  // object[start:stop]  (start and/or stop may be null)
    ExprPtr object;
    ExprPtr start;  // null -> from the beginning
    ExprPtr stop;   // null -> to the end
    Slice(ExprPtr o, ExprPtr a, ExprPtr b)
        : Expr(ExprKind::Slice), object(std::move(o)), start(std::move(a)), stop(std::move(b)) {}
};

struct Call : Expr {  // callee(args...)
    ExprPtr callee;
    std::vector<ExprPtr> args;
    Call(ExprPtr c, std::vector<ExprPtr> a)
        : Expr(ExprKind::Call), callee(std::move(c)), args(std::move(a)) {}
};

struct ListLit : Expr {  // [a, b, c]  -> std::vector
    std::vector<ExprPtr> elements;
    bool multiline = false;  // the `[`…`]` spanned >1 source line -> codegen keeps the
                             // generated C++ multi-line too (readable .gen.cpp).
    ListLit() : Expr(ExprKind::ListLit) {}
};

struct DictLit : Expr {  // {k: v, …}  -> std::unordered_map
    std::vector<ExprPtr> keys;
    std::vector<ExprPtr> values;
    bool multiline = false;  // as ListLit: a multi-line literal stays multi-line in codegen.
    DictLit() : Expr(ExprKind::DictLit) {}
};

// A struct designated initializer: `{.field = value, …}` (distinguished from a dict by the
// leading `.`). Used as the sole argument of a struct call — `Type({.f = v})` — and lowered
// to a C++20 designated initializer `Type{.f = v}`, so unspecified fields default-initialize.
struct StructInit : Expr {
    std::vector<std::string> fields;   // field names (without the leading '.')
    std::vector<ExprPtr> values;       // parallel to fields
    bool multiline = false;            // keep a multi-line initializer multi-line in codegen
    StructInit() : Expr(ExprKind::StructInit) {}
};

struct Unary : Expr {  // op operand  (op is the C++ operator: "-" or "!")
    std::string op;
    ExprPtr operand;
    Unary(std::string o, ExprPtr e) : Expr(ExprKind::Unary), op(std::move(o)), operand(std::move(e)) {}
};

struct Binary : Expr {  // lhs op rhs  (op is the C++ operator: "+","==","&&", …)
    std::string op;
    ExprPtr lhs;
    ExprPtr rhs;
    Binary(std::string o, ExprPtr l, ExprPtr r)
        : Expr(ExprKind::Binary), op(std::move(o)), lhs(std::move(l)), rhs(std::move(r)) {}
};

// A type reference: a primitive/struct name, or a container with type args.
//   int -> {name:"int"} ; list[float] -> {name:"list", args:[float]} ;
//   dict[str,int] -> {name:"dict", args:[str,int]} ; array[int,8] -> {…, array_size:"8"}
struct TypeRef {
    std::string name;
    std::vector<TypeRef> args;
    std::string array_size;  // only for array[T, N]
};

struct Field {
    std::string name;
    TypeRef type;
};

// ---- Statements ----
enum class StmtKind {
    Import, ExprStmt, Let, Assign, If, While, For, Return, Try, Raise, StructDef, FnDef, RawCpp,
    Break, Continue, Match, InterfaceDef, EnumDef,
};

struct Stmt {
    StmtKind kind;
    unsigned line = 0;  // 1-based source line (0 = unknown). Set by the parser; codegen
                        // emits `#line` from it so the C++ backend's diagnostics — and the
                        // editor's — point at the original .purr line.
    explicit Stmt(StmtKind k) : kind(k) {}
    virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;
using Block = std::vector<StmtPtr>;

struct Import : Stmt {  // import <module> [as <alias>]   (module is a dotted path)
    std::vector<std::string> module;
    std::string alias;  // empty if no `as`
    Import() : Stmt(StmtKind::Import) {}
};

struct ExprStmt : Stmt {
    ExprPtr expr;
    explicit ExprStmt(ExprPtr e) : Stmt(StmtKind::ExprStmt), expr(std::move(e)) {}
};

struct Let : Stmt {  // let <name> [: <type>] = <value>
    std::string name;
    bool has_type = false;  // an explicit `: <type>` annotation was given
    TypeRef type;           // valid iff has_type — declares the C++ type (e.g. empty list[int])
    ExprPtr value;
    Let() : Stmt(StmtKind::Let) {}
};

struct Assign : Stmt {  // <target> = <value>  (target is an lvalue: ident/member/index)
    ExprPtr target;
    ExprPtr value;
    Assign() : Stmt(StmtKind::Assign) {}
};

struct If : Stmt {
    ExprPtr cond;
    Block then_body;
    Block else_body;  // empty if no else
    If() : Stmt(StmtKind::If) {}
};

struct While : Stmt {
    ExprPtr cond;
    Block body;
    While() : Stmt(StmtKind::While) {}
};

struct For : Stmt {  // for <var> in <iterable> { … }
    std::string var;
    ExprPtr iterable;
    Block body;
    For() : Stmt(StmtKind::For) {}
};

struct Return : Stmt {
    ExprPtr value;  // may be null (`return`)
    Return() : Stmt(StmtKind::Return) {}
};

// try { … } except [name] { … }   — `name` (if present) binds the error message.
struct Try : Stmt {
    Block body;
    std::string catch_var;
    Block catch_body;
    Try() : Stmt(StmtKind::Try) {}
};

struct Raise : Stmt {  // raise <message>
    ExprPtr value;
    Raise() : Stmt(StmtKind::Raise) {}
};

struct Break : Stmt {  // break — exit the nearest loop
    Break() : Stmt(StmtKind::Break) {}
};

struct Continue : Stmt {  // continue — next iteration of the nearest loop
    Continue() : Stmt(StmtKind::Continue) {}
};

// match <subject> { case <expr> { … }  case _ { … } }  — value match; `_` is the
// default. Lowers to an if/else-if chain comparing the subject with `==`.
struct MatchCase {
    ExprPtr pattern;   // null when `wildcard` (the `_` default)
    bool wildcard = false;
    Block body;
};
struct Match : Stmt {
    ExprPtr subject;
    std::vector<MatchCase> cases;
    Match() : Stmt(StmtKind::Match) {}
};

struct StructDef : Stmt {  // struct Name [: Iface, …] { field: type … [fn method(self,…){…}] }
    std::string name;
    std::vector<Field> fields;
    std::vector<StmtPtr> methods;       // each is a FnDef; first param is `self`
    std::vector<std::string> fulfills;  // interfaces this struct must satisfy (-> static_assert)
    StructDef() : Stmt(StmtKind::StructDef) {}
};

struct FnDef : Stmt {  // fn <name>(<params>) { … }  (untyped params -> auto)
    std::string name;
    std::vector<std::string> params;
    std::vector<std::string> param_types;  // parallel to params; "" = untyped (auto). An
                                           // interface name here constrains that param.
    Block body;
    FnDef() : Stmt(StmtKind::FnDef) {}
};

// interface Name { fn method(self [, p: type]…) [-> type] … } — a contract that
// lowers to a C++ concept; a struct that lists it in `fulfills` is static_asserted.
struct InterfaceMethod {
    std::string name;
    std::vector<TypeRef> param_types;  // types of params AFTER self (self is the receiver)
    bool has_return = false;
    TypeRef return_type;
};
struct InterfaceDef : Stmt {
    std::string name;
    std::vector<InterfaceMethod> methods;
    InterfaceDef() : Stmt(StmtKind::InterfaceDef) {}
};

// enum Name { A [= expr], B, … } — a scoped, type-safe enumeration that lowers to a
// C++ `enum class`. Members are accessed scoped (`Name.A` -> `Name::A`); a value is
// an optional explicit initializer (an integer literal or a constant expression,
// which may reference earlier members of the same enum).
struct Enumerator {
    std::string name;
    ExprPtr value;  // null if no explicit `= …` value
};
struct EnumDef : Stmt {
    std::string name;
    std::vector<Enumerator> enumerators;
    EnumDef() : Stmt(StmtKind::EnumDef) {}
};

// Raw C++ escape hatch: `cpp { … }`. `code` is the verbatim body. Emitted at FILE
// SCOPE when written at the program top level (so it can carry #includes, helper
// functions, and types); emitted INLINE when written inside a function or block.
struct RawCpp : Stmt {
    std::string code;
    explicit RawCpp(std::string c) : Stmt(StmtKind::RawCpp), code(std::move(c)) {}
};

struct Program {
    std::vector<StmtPtr> body;
};

} // namespace cheatah
