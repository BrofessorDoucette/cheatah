// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// End-to-end pipeline test: compile a .purr with purrc into a loadable module,
// then run it with the cheatah runtime and check what it prints.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <sys/wait.h>

#include <gtest/gtest.h>

#ifndef PURRC_PATH
#define PURRC_PATH ""
#endif
#ifndef CHEATAH_RUNTIME_PATH
#define CHEATAH_RUNTIME_PATH ""
#endif
#ifndef PURR_TEST_TMP
#define PURR_TEST_TMP "."
#endif

namespace {

std::string run_capture(const std::string& cmd, int& exit_code) {
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        exit_code = -1;
        return out;
    }
    std::array<char, 256> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        out += buf.data();
    }
    const int status = pclose(pipe);
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return out;
}

} // namespace

TEST(PurrcPipeline, CompilesAndRunsHelloMeow) {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/hellomeow_it.purr";
    const std::string mod = tmp + "/hellomeow_it.so";

    {
        std::ofstream f(purr);
        f << "import io\n";
        f << "io.print(\"meow\")\n";
    }

    // 1) Compile with purrc.
    const std::string compile =
        std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(compile.c_str()), 0) << "purrc failed to compile the program";

    // 2) Run it with the cheatah runtime and capture stdout.
    int rc = -1;
    const std::string out = run_capture(std::string(CHEATAH_RUNTIME_PATH) + " \"" + mod + "\"", rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out, "meow\n");
}

TEST(PurrcPipeline, CompilesAndRunsStructsFunctionsAndLoops) {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/lang_it.purr";
    const std::string mod = tmp + "/lang_it.so";

    {
        std::ofstream f(purr);
        f << "import io\n";
        f << "struct P { x: int\n y: int }\n";
        f << "fn add(a, b) { return a + b }\n";
        f << "let total = 0\n";
        f << "for i in range(1, 5) { total = total + i }\n";  // 1+2+3+4 = 10
        f << "let p = P(2, 3)\n";
        f << "io.print(total, add(p.x, p.y))\n";              // "10 5"
    }

    const std::string compile =
        std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(compile.c_str()), 0) << "purrc failed to compile the program";

    int rc = -1;
    const std::string out = run_capture(std::string(CHEATAH_RUNTIME_PATH) + " \"" + mod + "\"", rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out, "10 5\n");
}

TEST(PurrcPipeline, CompilesAndRunsContainersAndStringConcat) {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/cont_it.purr";
    const std::string mod = tmp + "/cont_it.so";

    {
        std::ofstream f(purr);
        f << "import io\n";
        f << "let xs = [2, 3, 5]\n";
        f << "let sum = 0\n";
        f << "for x in xs { sum = sum + x }\n";           // 2+3+5 = 10
        f << "let m = {\"k\": 7}\n";
        f << "let name = \"hello\" + \"-world\"\n";         // string concat
        f << "io.print(len(xs), sum, xs[0], m[\"k\"], name)\n";
    }

    const std::string compile =
        std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(compile.c_str()), 0) << "purrc failed to compile the program";

    int rc = -1;
    const std::string out = run_capture(std::string(CHEATAH_RUNTIME_PATH) + " \"" + mod + "\"", rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out, "3 10 2 7 hello-world\n");
}

TEST(PurrcPipeline, CompilesAndRunsTryExceptRaise) {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/exc_it.purr";
    const std::string mod = tmp + "/exc_it.so";

    {
        std::ofstream f(purr);
        f << "import io\n";
        f << "try {\n";
        f << "  raise \"boom\"\n";
        f << "  io.print(\"unreachable\")\n";
        f << "} except e {\n";
        f << "  io.print(\"caught:\", e)\n";
        f << "}\n";
        f << "io.print(\"after\")\n";
    }

    const std::string compile =
        std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(compile.c_str()), 0) << "purrc failed to compile the program";

    int rc = -1;
    const std::string out = run_capture(std::string(CHEATAH_RUNTIME_PATH) + " \"" + mod + "\"", rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out, "caught: boom\nafter\n");
}

TEST(PurrcPipeline, CompilesAndRunsSemicolons) {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/semi_it.purr";
    const std::string mod = tmp + "/semi_it.so";

    {
        std::ofstream f(purr);
        f << "import io\n";
        f << "let a = 1; let b = 2;\n";                 // separator + terminator on one line
        f << "fn add(x, y) { return x + y; }\n";        // ; inside a function body
        f << "struct P { x: int; y: int }\n";           // ; as struct field separator
        f << "let p = P(3, 4)\n";
        f << "io.print(a, b, add(p.x, p.y));\n";        // 1 2 7
    }

    const std::string compile =
        std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(compile.c_str()), 0) << "purrc failed to compile the program";

    int rc = -1;
    const std::string out = run_capture(std::string(CHEATAH_RUNTIME_PATH) + " \"" + mod + "\"", rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out, "1 2 7\n");
}

TEST(PurrcPipeline, CompilesAndRunsCppEscapeHatch) {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/cpp_it.purr";
    const std::string mod = tmp + "/cpp_it.so";

    {
        std::ofstream f(purr);
        f << "import io\n";
        f << "cpp {\n";                                            // top-level -> file scope
        f << "static long long triple(long long n) { return n * 3; }\n";
        f << "}\n";
        f << "fn demo() {\n";
        f << "  let acc = 0\n";
        f << "  cpp {\n";                                          // nested -> inline
        f << "    for (int i = 1; i <= 4; ++i) { acc += i; }\n";   // 1+2+3+4 = 10
        f << "  }\n";
        f << "  return acc + triple(2)\n";                         // 10 + 6 = 16
        f << "}\n";
        f << "io.print(\"cpp escape hatch:\", demo())\n";
    }

    const std::string compile =
        std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(compile.c_str()), 0) << "purrc failed to compile the program";

    int rc = -1;
    const std::string out = run_capture(std::string(CHEATAH_RUNTIME_PATH) + " \"" + mod + "\"", rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out, "cpp escape hatch: 16\n");
}
