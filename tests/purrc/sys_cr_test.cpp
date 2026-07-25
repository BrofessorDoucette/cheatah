// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Compile-run unit tests for the `sys` module (suite SysCompileRun) PLUS its
// per-module system-level test (StdlibE2E.Sys — both live here because sys's
// whole surface is `sys.argv`). Each test writes a tiny .purr, compiles it with
// purrc, runs it under the cheatah runtime, and asserts the exact stdout.
// Complements the in-process unit tests (stdlib/tests/sys_test.cpp).
//
// The runtime invocation is what is under test: `cheatah <module> [args…]`
// forwards `<module>` + the user args through the cheatah_set_argv hook, so
// sys.argv[0] is the module path and sys.argv[1:] the user arguments (Python's
// convention). The module path is a build-dir temp path — NON-deterministic —
// so programs never print argv[0] raw; they assert deterministic PROPERTIES of
// it (non-empty, ends with ".so") and print only the user arguments, which the
// tests fully control.
#include "e2e_harness.hpp"

namespace {

// Like e2e::expect_e2e, but appends user arguments to the cheatah invocation —
// `cheatah <module> <args…>` — so a program can observe sys.argv[1:]. `args` is
// spliced into the shell command verbatim; callers pass pre-quoted words
// (e.g. "alpha 'two words' ''").
void expect_e2e_with_args(const std::string& name, const std::string& src,
                          const std::string& args, const std::string& expected) {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/" + name + "_e2e.purr";
    const std::string mod = tmp + "/" + name + "_e2e.so";
    { std::ofstream f(purr); f << src; }

    const std::string compile =
        std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(compile.c_str()), 0) << name << ": purrc failed to compile the program";

    int rc = -1;
    const std::string out = e2e::run_capture(
        "\"" + std::string(CHEATAH_RUNTIME_PATH) + "\" \"" + mod + "\" " + args, rc);
    EXPECT_EQ(rc, 0) << name << ": program exited non-zero";
    EXPECT_EQ(out, expected) << name << ": stdout mismatch";
}

}  // namespace

// With no user arguments, sys.argv is exactly [module]: length 1, and argv[0]
// is the module path the runtime was given (a real path to the loaded .so).
TEST(SysCompileRun, ArgvNoUserArguments) {
    e2e::expect_e2e("sys_argv_noargs", R"PURR(import io
import string
import sys
io.print(len(sys.argv))
io.print(len(sys.argv[0]) > 0, string.endswith(sys.argv[0], ".so"))
)PURR", "1\nTrue True\n");
}

// User arguments land in sys.argv[1:] in order, with argv[0] still the module.
TEST(SysCompileRun, ArgvForwardsUserArguments) {
    expect_e2e_with_args("sys_argv_forward", R"PURR(import io
import sys
io.print(len(sys.argv))
io.print(sys.argv[1], sys.argv[2], sys.argv[3])
)PURR", "alpha beta gamma", "4\nalpha beta gamma\n");
}

// Arguments are forwarded VERBATIM: an argument with an embedded space stays one
// argument, and an empty argument stays present (and empty) rather than dropped.
TEST(SysCompileRun, ArgvPreservesSpacedAndEmptyArguments) {
    expect_e2e_with_args("sys_argv_verbatim", R"PURR(import io
import sys
io.print(len(sys.argv))
io.print("[" + sys.argv[1] + "]")
io.print("[" + sys.argv[2] + "]")
)PURR", "'two words' ''", "3\n[two words]\n[]\n");
}

// sys.argv[1:] is the user-argument list — sliceable and joinable like any list.
TEST(SysCompileRun, ArgvSliceOfUserArguments) {
    expect_e2e_with_args("sys_argv_slice", R"PURR(import io
import string
import sys
io.print(string.join("|", sys.argv[1:]))
)PURR", "a b c", "a|b|c\n");
}

// sys.argv is iterable (`for a in sys.argv { … }`) — the loop sees every entry.
TEST(SysCompileRun, ArgvIsIterable) {
    expect_e2e_with_args("sys_argv_iter", R"PURR(import io
import sys
let n = 0
for a in sys.argv {
    n = n + 1
}
io.print(n, len(sys.argv))
io.print(n == len(sys.argv))
)PURR", "one two", "3 3\nTrue\n");
}

// System-level test: one cohesive program exercising the whole sys surface —
// length, the argv[0] module-path properties, indexing, slicing, and iteration —
// printing only deterministic values (the user args + boolean properties).
TEST(StdlibE2E, Sys) {
    expect_e2e_with_args("sys_sys", R"PURR(import io
import string
import sys

# The runtime forwarded [module, x, y, z].
io.print(len(sys.argv))

# argv[0] is the module path: never printed raw (build-dir path), only its properties.
io.print(len(sys.argv[0]) > 0, string.endswith(sys.argv[0], ".so"))

# Indexing and slicing follow Python: argv[1:] is exactly the user arguments.
io.print(sys.argv[1], sys.argv[2], sys.argv[3])
io.print(string.join(",", sys.argv[1:]))

# Iteration visits every entry once, in order (skip the path at index 0).
let seen = 0
let joined = ""
for a in sys.argv {
    if seen > 0 {
        joined = joined + "/" + a
    }
    seen = seen + 1
}
io.print(seen, joined)
)PURR", "x y z",
        "4\n"
        "True True\n"
        "x y z\n"
        "x,y,z\n"
        "4 /x/y/z\n");
}
