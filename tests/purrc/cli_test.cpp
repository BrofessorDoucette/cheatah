// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// CLI smoke tests for the toolchain executables: --help / -h print usage to stdout and
// exit 0, and --version reports the tool name. (biome is a cheatah program with its own
// `help`/`--help` command, covered by its own run.)
#include "e2e_harness.hpp"

namespace {
struct Proc { int code; std::string out; };
Proc run(const std::string& cmd) {
    int code = -1;
    const std::string out = e2e::run_capture(cmd, code);
    return {code, out};
}
const std::string PURRC = PURRC_PATH;
const std::string CHEATAH = CHEATAH_RUNTIME_PATH;
}  // namespace

TEST(Cli, PurrcHelp) {
    for (const char* flag : {"--help", "-h"}) {
        const Proc r = run(PURRC + " " + flag);
        EXPECT_EQ(r.code, 0) << flag;
        EXPECT_NE(r.out.find("usage: purrc"), std::string::npos) << flag << ": " << r.out;
        EXPECT_NE(r.out.find("--keygen"), std::string::npos) << flag;  // documents the new flags
    }
}

TEST(Cli, CheatahHelp) {
    for (const char* flag : {"--help", "-h"}) {
        const Proc r = run(CHEATAH + " " + flag);
        EXPECT_EQ(r.code, 0) << flag;
        EXPECT_NE(r.out.find("usage: cheatah"), std::string::npos) << flag << ": " << r.out;
        EXPECT_NE(r.out.find("--verify"), std::string::npos) << flag;
    }
}

TEST(Cli, Version) {
    const Proc p = run(PURRC + " --version");
    EXPECT_EQ(p.code, 0);
    EXPECT_NE(p.out.find("purrc"), std::string::npos) << p.out;
    const Proc c = run(CHEATAH + " --version");
    EXPECT_EQ(c.code, 0);
    EXPECT_NE(c.out.find("cheatah"), std::string::npos) << c.out;
}

// With no arguments, each prints usage to stderr and exits non-zero (a usage error).
TEST(Cli, NoArgsIsUsageError) {
    const Proc p = run(PURRC + " 2>&1");
    EXPECT_NE(p.code, 0);
    EXPECT_NE(p.out.find("usage: purrc"), std::string::npos) << p.out;
    const Proc c = run(CHEATAH + " 2>&1");
    EXPECT_NE(c.code, 0);
    EXPECT_NE(c.out.find("usage: cheatah"), std::string::npos) << c.out;
}

namespace {
// Write a .purr into the test temp dir and return its path (for `--check` tests).
std::string write_purr(const std::string& name, const std::string& src) {
    const std::string path = std::string(PURR_TEST_TMP) + "/" + name + ".purr";
    std::ofstream(path) << src;
    return path;
}
}  // namespace

// `--check` surfaces friendly, .purr-located diagnostics for `constexpr` misuse (the VS Code
// error provider runs this), BEFORE the cryptic C++ backend errors. Misuse exits non-zero
// with a clear message; valid constexpr code passes.
TEST(CliCheck, ReassignConstexprLetRejected) {
    const std::string p = write_purr("chk_reassign",
        "import io\nconstexpr let N = 4\nN = 5\nio.print(N)\n");
    const Proc r = run(PURRC + " --check \"" + p + "\" 2>&1");
    EXPECT_NE(r.code, 0) << r.out;
    EXPECT_NE(r.out.find("cannot reassign"), std::string::npos) << r.out;
    EXPECT_NE(r.out.find(":3:"), std::string::npos) << "should point at the reassignment line: " << r.out;
}

TEST(CliCheck, ConstexprLetNeedsInitializer) {
    const std::string p = write_purr("chk_noinit", "import io\nconstexpr let X\nio.print(1)\n");
    const Proc r = run(PURRC + " --check \"" + p + "\" 2>&1");
    EXPECT_NE(r.code, 0) << r.out;
    EXPECT_NE(r.out.find("needs an initializer"), std::string::npos) << r.out;
}

TEST(CliCheck, ValidConstexprPasses) {
    const std::string p = write_purr("chk_ok",
        "import io\nconstexpr let N = 4\nif (N == 4) { io.print(\"ok\") }\n");
    const Proc r = run(PURRC + " --check \"" + p + "\" 2>&1");
    EXPECT_EQ(r.code, 0) << r.out;
}

// A constexpr let shadowed by a plain `let` of the same name in an inner scope: assigning the
// INNER (runtime) binding must NOT be flagged — guards the scope-awareness (no false positive).
TEST(CliCheck, ShadowedConstexprNoFalsePositive) {
    const std::string p = write_purr("chk_shadow",
        "import io\nconstexpr let N = 4\nfn f() {\n  let N = 1\n  N = 2\n  return N\n}\nio.print(f())\n");
    const Proc r = run(PURRC + " --check \"" + p + "\" 2>&1");
    EXPECT_EQ(r.code, 0) << r.out;
}
