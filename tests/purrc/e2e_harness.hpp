// Shared compile-run harness for the cheatah end-to-end test suites.
//
// expect_e2e(name, src, expected[, env_prefix]) writes a .purr program, compiles
// it with purrc into a loadable module, runs it under the cheatah runtime, and
// asserts the captured stdout equals `expected` (byte-for-byte). Used by:
//   - the per-function "compile-run" tests   (tests/purrc/compile_run/*)
//   - the per-module  "system-level"  tests  (stdlib_e2e_test.cpp)
//
// Programs must be DETERMINISTIC (no clocks/PIDs/unseeded RNG; datetime under
// TZ=UTC; RNG seeded; floats use io.print's default 6-significant-digit form).
#pragma once

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

namespace e2e {

inline std::string run_capture(const std::string& cmd, int& exit_code) {
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

// Compile `src` with purrc, run the module under the cheatah runtime (optionally
// with an env prefix like "TZ=UTC "), and assert the captured stdout equals
// `expected`. `name` makes the temp files and failure messages unique.
inline void expect_e2e(const std::string& name, const std::string& src,
                       const std::string& expected, const std::string& env_prefix = "") {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/" + name + "_e2e.purr";
    const std::string mod = tmp + "/" + name + "_e2e.so";
    { std::ofstream f(purr); f << src; }

    const std::string compile =
        std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(compile.c_str()), 0) << name << ": purrc failed to compile the program";

    int rc = -1;
    const std::string out =
        run_capture(env_prefix + "\"" + std::string(CHEATAH_RUNTIME_PATH) + "\" \"" + mod + "\"", rc);
    EXPECT_EQ(rc, 0) << name << ": program exited non-zero";
    EXPECT_EQ(out, expected) << name << ": stdout mismatch";
}

}  // namespace e2e
