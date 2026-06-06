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
    EXPECT_TRUE(contains(cg.source, "extern \"C\" void purr_main()"));
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
    EXPECT_TRUE(contains(cg.source, "auto add(auto a, auto b) {"));  // C++20 abbreviated template
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
