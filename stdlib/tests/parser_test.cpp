// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "ast.hpp"
#include "parser.hpp"

#include <gtest/gtest.h>

using namespace cheatah;

TEST(CheatahParser, ParsesImportAndModuleCall) {
    const ParseResult r = parse_source("import io\nio.print(\"meow\")\n");
    ASSERT_TRUE(r.ok()) << (r.diagnostics.empty() ? "" : r.diagnostics.front().message);
    ASSERT_EQ(r.program.body.size(), 2u);

    auto* imp = dynamic_cast<Import*>(r.program.body[0].get());
    ASSERT_NE(imp, nullptr);
    ASSERT_EQ(imp->module.size(), 1u);
    EXPECT_EQ(imp->module[0], "io");
    EXPECT_TRUE(imp->alias.empty());

    auto* es = dynamic_cast<ExprStmt*>(r.program.body[1].get());
    ASSERT_NE(es, nullptr);
    auto* call = dynamic_cast<Call*>(es->expr.get());
    ASSERT_NE(call, nullptr);
    ASSERT_EQ(call->args.size(), 1u);
    auto* arg = dynamic_cast<StringLit*>(call->args[0].get());
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(arg->value, "meow");

    auto* member = dynamic_cast<Member*>(call->callee.get());
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->name, "print");
    auto* obj = dynamic_cast<Ident*>(member->object.get());
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->name, "io");
}

TEST(CheatahParser, ImportWithDottedModuleAndAlias) {
    const ParseResult r = parse_source("import os.path as p\n");
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.program.body.size(), 1u);
    auto* imp = dynamic_cast<Import*>(r.program.body[0].get());
    ASSERT_NE(imp, nullptr);
    ASSERT_EQ(imp->module.size(), 2u);
    EXPECT_EQ(imp->module[0], "os");
    EXPECT_EQ(imp->module[1], "path");
    EXPECT_EQ(imp->alias, "p");
}

TEST(CheatahParser, ReportsErrorOnMissingModuleName) {
    const ParseResult r = parse_source("import\n");
    EXPECT_FALSE(r.ok());
}

TEST(CheatahParser, ParsesStructWithTypedFields) {
    const ParseResult r = parse_source("struct Bar {\ndate: str\nclose: float\n}\n");
    ASSERT_TRUE(r.ok()) << (r.diagnostics.empty() ? "" : r.diagnostics.front().message);
    ASSERT_EQ(r.program.body.size(), 1u);
    auto* sd = dynamic_cast<StructDef*>(r.program.body[0].get());
    ASSERT_NE(sd, nullptr);
    EXPECT_EQ(sd->name, "Bar");
    ASSERT_EQ(sd->fields.size(), 2u);
    EXPECT_EQ(sd->fields[0].name, "date");
    EXPECT_EQ(sd->fields[0].type.name, "str");
    EXPECT_EQ(sd->fields[1].name, "close");
    EXPECT_EQ(sd->fields[1].type.name, "float");
}

TEST(CheatahParser, ParsesEnumWithOptionalValues) {
    const ParseResult r = parse_source("enum Status {\nOK = 0\nWARN\nFAIL = 2\n}\n");
    ASSERT_TRUE(r.ok()) << (r.diagnostics.empty() ? "" : r.diagnostics.front().message);
    ASSERT_EQ(r.program.body.size(), 1u);
    auto* ed = dynamic_cast<EnumDef*>(r.program.body[0].get());
    ASSERT_NE(ed, nullptr);
    EXPECT_EQ(ed->name, "Status");
    ASSERT_EQ(ed->enumerators.size(), 3u);
    EXPECT_EQ(ed->enumerators[0].name, "OK");
    EXPECT_NE(ed->enumerators[0].value, nullptr);    // OK = 0
    EXPECT_EQ(ed->enumerators[1].name, "WARN");
    EXPECT_EQ(ed->enumerators[1].value, nullptr);    // no explicit value
    EXPECT_EQ(ed->enumerators[2].name, "FAIL");
    EXPECT_NE(ed->enumerators[2].value, nullptr);    // FAIL = 2
}

TEST(CheatahParser, ParsesFunctionWithParamsAndReturn) {
    const ParseResult r = parse_source("fn add(a, b) {\nreturn a + b\n}\n");
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.program.body.size(), 1u);
    auto* fd = dynamic_cast<FnDef*>(r.program.body[0].get());
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->name, "add");
    ASSERT_EQ(fd->params.size(), 2u);
    EXPECT_EQ(fd->params[0], "a");
    ASSERT_EQ(fd->body.size(), 1u);
    auto* ret = dynamic_cast<Return*>(fd->body[0].get());
    ASSERT_NE(ret, nullptr);
    auto* bin = dynamic_cast<Binary*>(ret->value.get());
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, "+");
}

TEST(CheatahParser, ParsesLetWhileForIfWithOperators) {
    const ParseResult r = parse_source(
        "let n = 0\nwhile n < 10 {\nn = n + 1\n}\nfor i in range(3) {\nn = n * 2\n}\n"
        "if n >= 5 and n != 7 {\nn = 0\n}\n");
    ASSERT_TRUE(r.ok()) << (r.diagnostics.empty() ? "" : r.diagnostics.front().message);
    ASSERT_EQ(r.program.body.size(), 4u);
    EXPECT_NE(dynamic_cast<Let*>(r.program.body[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<While*>(r.program.body[1].get()), nullptr);
    EXPECT_NE(dynamic_cast<For*>(r.program.body[2].get()), nullptr);
    EXPECT_NE(dynamic_cast<If*>(r.program.body[3].get()), nullptr);
}

TEST(CheatahParser, SemicolonsSeparateAndTerminateStatements) {
    // `;` separates statements on a line and may terminate one; never required.
    const ParseResult r = parse_source("let a = 1; let b = 2;\nlet c = 3\n");
    ASSERT_TRUE(r.ok()) << (r.diagnostics.empty() ? "" : r.diagnostics.front().message);
    ASSERT_EQ(r.program.body.size(), 3u);
    EXPECT_NE(dynamic_cast<Let*>(r.program.body[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<Let*>(r.program.body[1].get()), nullptr);
    EXPECT_NE(dynamic_cast<Let*>(r.program.body[2].get()), nullptr);
}

TEST(CheatahParser, SemicolonSeparatesStructFields) {
    const ParseResult r = parse_source("struct P { x: int; y: int }\n");
    ASSERT_TRUE(r.ok()) << (r.diagnostics.empty() ? "" : r.diagnostics.front().message);
    ASSERT_EQ(r.program.body.size(), 1u);
    auto* sd = dynamic_cast<StructDef*>(r.program.body[0].get());
    ASSERT_NE(sd, nullptr);
    EXPECT_EQ(sd->fields.size(), 2u);
}

TEST(CheatahParser, OperatorPrecedence) {
    // 1 + 2 * 3  ->  (1 + (2 * 3))
    const ParseResult r = parse_source("let x = 1 + 2 * 3\n");
    ASSERT_TRUE(r.ok());
    auto* let = dynamic_cast<Let*>(r.program.body[0].get());
    ASSERT_NE(let, nullptr);
    auto* top = dynamic_cast<Binary*>(let->value.get());
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->op, "+");
    auto* rhs = dynamic_cast<Binary*>(top->rhs.get());
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->op, "*");  // multiplication binds tighter
}

// Regression: a stray top-level `}` (e.g. from an unrecognized continuation) must
// produce a diagnostic and RETURN — parse_stmts must never spin forever on a token
// that synchronize() can't advance past (which used to OOM the compiler).
TEST(CheatahParser, StrayTopLevelBraceErrorsWithoutHanging) {
    const ParseResult r = parse_source("import io\nio.print(1)\n}\n");
    EXPECT_FALSE(r.ok());                  // reports the stray brace
    EXPECT_FALSE(r.diagnostics.empty());
    EXPECT_GE(r.program.body.size(), 2u);  // the valid statements before it still parsed
}

// Multi-line `elif`/`else` (on their own lines after the `if` block) parse like the
// same-line form: the `elif` becomes a chained If in the else branch.
TEST(CheatahParser, ParsesMultilineElifElse) {
    const ParseResult r = parse_source(
        "let r = 0\nif r < 0 {\nr = 1\n}\nelif r == 0 {\nr = 2\n}\nelse {\nr = 3\n}\n");
    ASSERT_TRUE(r.ok()) << (r.diagnostics.empty() ? "" : r.diagnostics.front().message);
    auto* iff = dynamic_cast<If*>(r.program.body.back().get());
    ASSERT_NE(iff, nullptr);
    ASSERT_EQ(iff->else_body.size(), 1u);
    EXPECT_NE(dynamic_cast<If*>(iff->else_body[0].get()), nullptr);
}
