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
    // purr_main is emitted through the portable export macro (C linkage everywhere,
    // dllexport on Windows so the DLL exposes the entry point).
    EXPECT_TRUE(contains(cg.source, "PURR_EXPORT void purr_main()"));
    EXPECT_TRUE(contains(cg.source, "#define PURR_EXPORT extern \"C\""));
    EXPECT_TRUE(contains(cg.source, "cheatah::io::print(std::string(\"meow\"));"));

    ASSERT_EQ(cg.modules.size(), 1u);
    EXPECT_EQ(cg.modules[0], "io");
}

TEST(CheatahCodegen, ResolvesNestedSubmoduleCalls) {
    const ParseResult pr = parse_source("import os\nos.path.join(\"a\", \"b\")\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    // The whole module chain qualifies with '::', not a stray '.'.
    EXPECT_TRUE(contains(cg.source,
                         "cheatah::os::path::join(std::string(\"a\"), std::string(\"b\"));"));
}

TEST(CheatahCodegen, BuiltinsResolveWithoutImport) {
    // No `import` — len/hex are always-available built-ins.
    const ParseResult pr = parse_source("len(\"meow\")\nhex(255)\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "#include \"builtins.hpp\""));
    EXPECT_TRUE(contains(cg.source, "cheatah::builtins::len(std::string(\"meow\"));"));
    EXPECT_TRUE(contains(cg.source, "cheatah::builtins::hex(255LL);"));
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

TEST(CheatahCodegen, EmitsFunctionAndCall) {
    const ParseResult pr = parse_source("fn add(a, b) {\n  return a + b\n}\nlet x = add(1, 2)\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    // C++20 abbreviated template, each param constrained by the baseline `Value`
    // concept (no generated template is left unconstrained).
    EXPECT_TRUE(contains(
        cg.source,
        "auto add(cheatah::builtins::Value auto a, cheatah::builtins::Value auto b) {"));
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
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "auto p = P{1LL, 2LL};"));  // P{...} not P(...)
}

TEST(CheatahCodegen, StructMethodBecomesMemberFunction) {
    const ParseResult pr = parse_source(
        "struct Circle {\nr: float\nfn area(self) {\nreturn self.r * self.r\n}\n}\n"
        "let c = Circle(2.0)\nlet a = c.area()\n");
    ASSERT_TRUE(pr.ok());
    const CodegenResult cg = codegen(pr.program);
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
    EXPECT_TRUE(contains(cg.source, "describe(Shape auto s)"));           // interface-typed param
}

TEST(CheatahCodegen, ContainerFieldTypes) {
    const ParseResult pr = parse_source(
        "struct S {\nprices: list[float]\nquotes: dict[str, float]\nbuf: array[int, 8]\n}\n");
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
    const CodegenResult cg = codegen(pr.program);
    ASSERT_TRUE(cg.ok());
    EXPECT_TRUE(contains(cg.source, "auto xs = std::vector{1LL, 2LL, 3LL};"));  // CTAD -> vector<int>
    EXPECT_TRUE(contains(cg.source, "std::unordered_map{std::pair{std::string(\"a\"), 1LL}}"));
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
    EXPECT_TRUE(contains(cg.source, "throw std::runtime_error(std::string(\"boom\"));"));
    EXPECT_TRUE(contains(cg.source, "} catch (const std::exception& e_exc) {"));
    EXPECT_TRUE(contains(cg.source, "auto e = std::string(e_exc.what());"));
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
