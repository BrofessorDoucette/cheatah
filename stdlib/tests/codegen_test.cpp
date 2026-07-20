// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "codegen.hpp"
#include "parser.hpp"

#include <string>

#include <gtest/gtest.h>

using namespace cheatah;

namespace {
bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}
} // namespace

TEST(CheatahCodegen, EmitsPurrMainWithNamespaceQualifiedCall) {
    const ParseResult pr = parse_source("import io\nio.print(\"meow\")\n");
    ASSERT_TRUE(pr.ok());

    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "#include \"io.hpp\""));
    // The prelude (std headers + built-ins + the PURR_EXPORT macro) comes in through
    // one consolidated include rather than a dozen repeated #includes per file.
    EXPECT_TRUE(contains(cg.source, "#include \"cheatah.hpp\""));
    // purr_main is emitted through the portable export macro (defined in the prelude:
    // C linkage everywhere, dllexport on Windows so the DLL exposes the entry point).
    EXPECT_TRUE(contains(cg.source, "PURR_EXPORT void purr_main()"));
    // The program lives in a dedicated namespace and the exported entry is a trampoline
    // into it — this is what keeps the module aliases from clashing with global symbols.
    EXPECT_TRUE(contains(cg.source, "namespace cheatah_program {"));
    EXPECT_TRUE(contains(cg.source,
                         "PURR_EXPORT void purr_main() { cheatah_program::purr_main(); }"));
    // Each imported module gets its own short namespace alias, so the call reads
    // `io::print` (not `cheatah::io::print`). io.print also only streams its args, so a
    // string literal passes as a bare const char* — no throwaway std::string.
    EXPECT_TRUE(contains(cg.source, "namespace io = ::cheatah::io;"));
    EXPECT_TRUE(contains(cg.source, "io::print(\"meow\");"));

    ASSERT_EQ(cg.modules.size(), 1u);
    EXPECT_EQ(cg.modules[0], "io");
}

TEST(CheatahCodegen, WithStatementLowersToScopedBlock) {
    // `with EXPR as NAME { … }` lowers to a plain nested block binding NAME to EXPR, so the
    // resource's RAII destructor runs exactly at block exit. Lean: a bare `auto` binding and a
    // scope — no temporaries, no helper calls, no context-manager machinery.
    const ParseResult pr = parse_source(
        "import socket\n"
        "with socket.open(\"h\", 80) as c {\n"
        "    c.send(\"x\")\n"
        "}\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program, "", false);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "auto c = socket::open("));  // bound to a named local
    EXPECT_TRUE(contains(cg.source, "c.send("));                 // body runs with c in scope
}

TEST(CheatahCodegen, WithWithoutAsBindsHiddenLocal) {
    // `with EXPR { … }` (no `as`) still binds EXPR to a hidden local, so the destructor fires at
    // block end rather than immediately — a discarded temporary would be destroyed too early.
    const ParseResult pr = parse_source(
        "import io\n"
        "with io.open(\"f\", \"r\") {\n"
        "    io.print(\"in\")\n"
        "}\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program, "", false);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "auto _purr_with_0 = io::open("));
}

TEST(CheatahCodegen, StreamingCallsTakeBareStringLiterals) {
    // io.print streams its args and io.format takes a std::string_view format — both
    // accept a literal as a bare const char*, no intermediate std::string. A literal
    // bound to a variable or concatenated still becomes a std::string (it must, to be a
    // first-class cheatah string).
    // Lowering test: keep the unused `let s` by disabling dead-variable removal (DCE has
    // its own tests) so the bound-literal -> std::string lowering is observable.
    const CodegenResult cg = codegen(
        parse_source("import io\nio.print(\"a\", io.format(\"{}\", 1))\nlet s = \"b\"\n").program,
        "", false);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "io::print(\"a\""));
    EXPECT_TRUE(contains(cg.source, "io::format(\"{}\""));
    EXPECT_FALSE(contains(cg.source, "std::string(\"a\")"));
    EXPECT_TRUE(contains(cg.source, "auto s = std::string(\"b\");"));  // bound var stays std::string
}

TEST(CheatahCodegen, ResolvesNestedSubmoduleCalls) {
    const ParseResult pr = parse_source("import os\nos.path.join(\"a\", \"b\")\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    // The whole module chain qualifies with '::', not a stray '.'. The root module is
    // aliased (`namespace os = ::cheatah::os;`), so the submodule reads `os::path::join`.
    EXPECT_TRUE(contains(cg.source, "namespace os = ::cheatah::os;"));
    EXPECT_TRUE(contains(cg.source,
                         "os::path::join(std::string(\"a\"), std::string(\"b\"));"));
}

TEST(CheatahCodegen, DottedImportDoesNotDoubleTheSubmoduleSegment) {
    // A DOTTED `import a.b` binds only the head `a` (Python semantics), so a call written in
    // full — `a.b.fn(...)` — qualifies to `a::b::fn`, NOT `a::b::b::fn`. (Regression: binding
    // the whole path to `a` re-appended the tail, breaking every `import os.path` + os.path.*.)
    const CodegenResult cg = codegen(parse_source(
        "import os.path\nos.path.join(\"a\", \"b\")\n").program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "os::path::join(std::string(\"a\"), std::string(\"b\"));"));
    EXPECT_FALSE(contains(cg.source, "os::path::path::join"));  // the doubled form must not appear
}

TEST(CheatahCodegen, AliasedSubmoduleImportResolvesToFullPath) {
    // `import a.b as x` binds the FULL path to `x`, so `x.fn(...)` reads `a::b::fn`.
    const CodegenResult cg = codegen(parse_source(
        "import os.path as p\np.join(\"a\", \"b\")\n").program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "os::path::join(std::string(\"a\"), std::string(\"b\"));"));
    EXPECT_FALSE(contains(cg.source, "os::path::path::join"));
}

TEST(CheatahCodegen, NamespaceAliasSkippedWhenNameCollides) {
    // A program identifier named like an imported module (here a `struct os`) would
    // clash with `namespace os = cheatah::os;`, so the codegen must NOT alias os — it
    // stays fully qualified. A non-colliding module (io) is still aliased.
    const CodegenResult cg = codegen(parse_source(
        "import os\nimport io\nstruct os { n: int }\nlet p = os(1)\n"
        "io.print(os.path.join(\"a\", \"b\"), p.n)\n").program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "namespace io = ::cheatah::io;"));   // safe -> aliased
    EXPECT_FALSE(contains(cg.source, "namespace os = ::cheatah::os;"));  // collides -> explicit
    EXPECT_TRUE(contains(cg.source, "::cheatah::os::path::join"));       // stays fully qualified
    EXPECT_TRUE(contains(cg.source, "struct os {"));
}

TEST(CheatahCodegen, BareIdentShadowingModuleStaysLocal) {
    // A parameter (or any local) named like an imported module shadows it: a bare
    // standalone `math` is the local, NOT the `cheatah::math` namespace.
    const CodegenResult cg = codegen(parse_source(
        "import math\nfn bump(math) {\n    return math + 1\n}\n").program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "return (math + 1LL);"));
    EXPECT_FALSE(contains(cg.source, "cheatah::math + 1LL"));
}

TEST(CheatahCodegen, BuiltinsResolveWithoutImport) {
    // No `import` — len/hex are always-available built-ins.
    const ParseResult pr = parse_source("len(\"meow\")\nhex(255)\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    // builtins arrives through the prelude (cheatah.hpp), not a direct include.
    EXPECT_TRUE(contains(cg.source, "#include \"cheatah.hpp\""));
    // builtins is aliased like every other module, so calls read `builtins::len`.
    EXPECT_TRUE(contains(cg.source, "namespace builtins = ::cheatah::builtins;"));
    EXPECT_TRUE(contains(cg.source, "builtins::len(std::string(\"meow\"));"));
    EXPECT_TRUE(contains(cg.source, "builtins::hex(255LL);"));
}


TEST(CheatahCodegen, DivisionLowersToTrueDivAndFloorDiv) {
    // `/` is Python-3 true division (always float) -> builtins::truediv;
    // `//` is opt-in floor division -> builtins::floordiv. Both pull in builtins.
    const ParseResult pr = parse_source("let a = 7 / 2\nlet b = 7 // 2\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program, "", false);  // keep unused a/b to see the lowering
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "builtins::truediv(7LL, 2LL)"));
    EXPECT_TRUE(contains(cg.source, "builtins::floordiv(7LL, 2LL)"));
}

TEST(CheatahCodegen, SelfAppendChainsIntoOneStatement) {
    // `x = x + "lit" + y` lowers to a single CHAINED in-place append —
    // `(x += "lit") += y;` — not three `x += …` lines and not an intermediate
    // std::string. The literal appends as a bare const char*; operator+= chains
    // because it returns a reference to x.
    const ParseResult pr =
        parse_source("let head = \"a\"\nlet y = \"b\"\nhead = head + \"lit\" + y\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "(head += \"lit\") += y;"));   // one chained statement
    EXPECT_FALSE(contains(cg.source, "std::string(\"lit\")"));     // no temporary
    // A single appended operand stays a plain `x += e;` (nothing to chain).
    const CodegenResult one = codegen(parse_source("let s = \"a\"\ns = s + \"b\"\n").program);
    ASSERT_TRUE(one.ok());
    EXPECT_TRUE(contains(one.source, "s += \"b\";"));
}

TEST(CheatahCodegen, EmitsStructDefinition) {
    const ParseResult pr = parse_source("struct Bar {\n  date: str\n  close: float\n}\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "struct Bar {"));
    EXPECT_TRUE(contains(cg.source, "std::string date;"));
    EXPECT_TRUE(contains(cg.source, "double close;"));
}

TEST(CheatahCodegen, EmitsScopedEnumClass) {
    const ParseResult pr = parse_source("enum Color {\n  RED\n  GREEN\n  BLUE\n}\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "enum class Color {"));  // scoped, not a plain enum
    EXPECT_TRUE(contains(cg.source, "RED,"));
    EXPECT_TRUE(contains(cg.source, "BLUE,"));
    // A streamable debug form so io.print(Color.RED) shows "Color.RED".
    EXPECT_TRUE(contains(cg.source, "std::ostream& operator<<(std::ostream& os_, Color"));
    EXPECT_TRUE(contains(cg.source, "return os_ << \"Color.RED\""));
}

TEST(CheatahCodegen, EnumExplicitValuesAndScopedMemberAccess) {
    const ParseResult pr = parse_source(
        "enum Status {\n  OK = 0\n  FAIL = 1\n}\nlet s = Status.FAIL\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program, "", false);  // keep unused s to see the lowering
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "OK = 0LL,"));            // explicit value
    EXPECT_TRUE(contains(cg.source, "auto s = Status::FAIL;"));  // EnumName.MEMBER -> ::
}

TEST(CheatahCodegen, EmitsFunctionAndCall) {
    const ParseResult pr = parse_source("fn add(a, b) {\n  return a + b\n}\nlet x = add(1, 2)\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program, "", false);  // keep unused x to see the call binding
    ASSERT_TRUE(cg.ok());
    // C++20 abbreviated template, each param constrained by the baseline `Value`
    // concept (no generated template is left unconstrained).
    EXPECT_TRUE(contains(
        cg.source,
        "auto add(builtins::Value auto&& a, builtins::Value auto&& b) {"));
    EXPECT_TRUE(contains(cg.source, "return (a + b);"));
    EXPECT_TRUE(contains(cg.source, "auto x = add(1LL, 2LL);"));
}

TEST(CheatahCodegen, EmitsControlFlow) {
    const ParseResult pr = parse_source(
        "let n = 0\nwhile n < 3 {\nn = n + 1\n}\nfor i in range(0, 2) {\nn = n + i\n}\n"
        "if n > 0 {\nn = n\n} else {\nn = 0\n}\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "while ((n < 3LL)) {"));
    EXPECT_TRUE(contains(cg.source, "for (long long i = 0LL; i < 2LL; ++i) {"));
    EXPECT_TRUE(contains(cg.source, "if ((n > 0LL)) {"));
    EXPECT_TRUE(contains(cg.source, "} else {"));
}

TEST(CheatahCodegen, StructConstructionUsesAggregateBraces) {
    const ParseResult pr = parse_source("struct P {\nx: int\ny: int\n}\nlet p = P(1, 2)\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program, "", false);  // keep unused p to see the construction
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "auto p = P{1LL, 2LL};"));  // P{...} not P(...)
}

TEST(CheatahCodegen, StructMethodBecomesMemberFunction) {
    const ParseResult pr = parse_source(
        "struct Circle {\nr: float\nfn area(self) {\nreturn self.r * self.r\n}\n}\n"
        "let c = Circle(2.0)\nlet a = c.area()\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program, "", false);  // keep unused a to see the member call
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "double r;"));
    EXPECT_TRUE(contains(cg.source, "auto area() const {"));     // non-mutating -> const; self is (*this)
    EXPECT_TRUE(contains(cg.source, "return ((*this).r * (*this).r);"));
    EXPECT_TRUE(contains(cg.source, "auto a = c.area();"));      // call is a direct member call
}

TEST(CheatahCodegen, InterfaceBecomesConceptAndStaticAssert) {
    const ParseResult pr = parse_source(
        "interface Shape {\nfn area(self)\n}\n"
        "struct Circle : Shape {\nr: float\nfn area(self) {\nreturn self.r\n}\n}\n"
        "fn describe(s: Shape) {\nreturn s.area()\n}\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "concept Shape ="));                  // interface -> concept
    EXPECT_TRUE(contains(cg.source, "requires(Self& self) { self.area(); }"));
    EXPECT_TRUE(contains(cg.source, "static_assert(Shape<Circle>"));      // fulfillment check
    EXPECT_TRUE(contains(cg.source, "describe(Shape auto&& s)"));           // interface-typed param
}

TEST(CheatahCodegen, ContainerFieldTypes) {
    const ParseResult pr = parse_source(
        "struct S {\nprices: list<float>\nquotes: dict<str, float>\nbuf: array<int, 8>\n}\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "std::vector<double> prices;"));
    EXPECT_TRUE(contains(cg.source, "std::unordered_map<std::string, double> quotes;"));
    EXPECT_TRUE(contains(cg.source, "std::array<long long, 8> buf;"));
}

TEST(CheatahCodegen, ListAndDictLiterals) {
    const ParseResult pr = parse_source("let xs = [1, 2, 3]\nlet m = {\"a\": 1}\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program, "", false);  // keep unused xs/m to see the literals
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "auto xs = std::vector{1LL, 2LL, 3LL};"));  // CTAD -> vector<int>
    EXPECT_TRUE(contains(cg.source, "std::unordered_map{std::pair{std::string(\"a\"), 1LL}}"));
}

// ---- Dead-variable elimination (default ON; purrc --no-remove-variables opts out) ----

TEST(CheatahCodegen, RemovesUnusedPureLocal) {
    // An unused `let` with a side-effect-free initializer is dropped entirely.
    const ParseResult pr = parse_source("let unused = 5\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);  // default remove_unused = true
    ASSERT_TRUE(cg.ok());
    EXPECT_FALSE(contains(cg.source, "unused"));
}

TEST(CheatahCodegen, UnusedLocalKeepsSideEffectingCall) {
    // The variable is removed but a call initializer is preserved as an expression
    // statement, so side effects survive: `let A = f()` -> `f();`.
    const ParseResult pr = parse_source("fn f() {\nreturn 1\n}\nlet A = f()\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_FALSE(contains(cg.source, "auto A = f();"));  // the unused variable is gone
    EXPECT_TRUE(contains(cg.source, "    f();"));          // the call is kept
}

TEST(CheatahCodegen, KeepsReturnedLocal) {
    // A returned local is read by its `return`, so it is never removed — an exported
    // function's result must survive even if nothing else in the body uses it.
    const ParseResult pr = parse_source("fn g() {\nlet r = 7\nreturn r\n}\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "auto r = 7LL;"));
    EXPECT_TRUE(contains(cg.source, "return r;"));
}

TEST(CheatahCodegen, KeepsUsedLocal) {
    const ParseResult pr = parse_source("import io\nlet x = 5\nio.print(x)\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "auto x = 5LL;"));
}

TEST(CheatahCodegen, NoRemoveVariablesKeepsUnusedLocal) {
    // remove_unused=false (the path purrc's --no-remove-variables / --no-optimize-cpp take)
    // emits every binding verbatim.
    const ParseResult pr = parse_source("let unused = 5\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program, "", false);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "auto unused = 5LL;"));
}

TEST(CheatahCodegen, IteratesOverContainer) {
    const ParseResult pr = parse_source("let xs = [10, 20]\nfor x in xs {\nx = x\n}\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "for (auto& x : xs) {"));  // range-based for
}

TEST(CheatahCodegen, EmitsTryExceptAndRaise) {
    const ParseResult pr = parse_source(
        "import io\ntry {\nraise \"boom\"\n} except e {\nio.print(e)\n}\n");
    ASSERT_TRUE(pr.ok()) << (pr.diagnostics.empty() ? "" : pr.diagnostics.front().message);
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "throw ::cheatah::builtins::Error(std::string(\"boom\"));"));
    // ONE `catch (...)` normalized through current_error(), not a catch per exception type: that is
    // what lets a raised Error, any std::exception, and a throw of an unknown type reach the same
    // handler instead of the last one escaping to std::terminate.
    EXPECT_TRUE(contains(cg.source, "} catch (...) {"));
    EXPECT_TRUE(contains(cg.source, "= ::cheatah::builtins::current_error();"));
    EXPECT_TRUE(contains(cg.source, "const auto& e = _purr_err;"));
}

TEST(CheatahCodegen, EmitsKindMatchingHandlersAndRethrowsWhatNoneClaim) {
    const ParseResult pr = parse_source(
        "import io\ntry {\nraise Error(\"oom\", \"no room\")\n} except e of \"oom\" {\nio.print(e)\n}\n");
    ASSERT_TRUE(pr.ok()) << (pr.diagnostics.empty() ? "" : pr.diagnostics.front().message);
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "_purr_err.kind() == std::string(\"oom\")"));
    // No catch-all clause, so anything else keeps travelling. Swallowing it would turn a handler that
    // names ONE kind into a blanket suppressor of every other failure.
    EXPECT_TRUE(contains(cg.source, "throw;"));
}

TEST(CheatahCodegen, EmitsFinallyAsAScopeGuard) {
    const ParseResult pr = parse_source(
        "import io\ntry {\nio.print(\"a\")\n} except e {\nio.print(e)\n} finally {\nio.print(\"z\")\n}\n");
    ASSERT_TRUE(pr.ok()) << (pr.diagnostics.empty() ? "" : pr.diagnostics.front().message);
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    // A guard, not a duplicated block — duplicating would skip the body on return/break/continue,
    // which is exactly when cleanup matters.
    EXPECT_TRUE(contains(cg.source, "::cheatah::builtins::make_finally("));
}

TEST(CheatahCodegen, EmitsBareRaiseAsRethrow) {
    const ParseResult pr = parse_source(
        "try {\nraise \"x\"\n} except e {\nraise\n}\n");
    ASSERT_TRUE(pr.ok()) << (pr.diagnostics.empty() ? "" : pr.diagnostics.front().message);
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    // `throw;` preserves the original error rather than reconstructing an approximation of it.
    EXPECT_TRUE(contains(cg.source, "throw;"));
}

TEST(CheatahCodegen, EscapesStringLiterals) {
    const ParseResult pr = parse_source("import io\nio.print(\"a\\tb\")\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "\"a\\tb\""));  // tab stays escaped in the literal
}

TEST(CheatahCodegen, CppBlockTopLevelIsFileScopeNestedIsInline) {
    const ParseResult pr = parse_source(
        "cpp {\nstatic int helper() { return 7; }\n}\n"
        "fn f() {\ncpp { int x = 1; }\n}\n");
    ASSERT_TRUE(pr.ok()) << (pr.diagnostics.empty() ? "" : pr.diagnostics.front().message);
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    // Raw bodies are emitted verbatim.
    EXPECT_TRUE(contains(cg.source, "static int helper() { return 7; }"));
    EXPECT_TRUE(contains(cg.source, "int x = 1;"));
    // The top-level block lands at FILE SCOPE — before purr_main.
    const auto helper_pos = cg.source.find("static int helper()");
    const auto main_pos = cg.source.find("purr_main");
    ASSERT_NE(helper_pos, std::string::npos);
    ASSERT_NE(main_pos, std::string::npos);
    EXPECT_LT(helper_pos, main_pos);
    // The nested block lands INSIDE the function f().
    const auto f_pos = cg.source.find("auto f(");
    const auto inline_pos = cg.source.find("int x = 1;");
    ASSERT_NE(f_pos, std::string::npos);
    EXPECT_GT(inline_pos, f_pos);
}
