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
