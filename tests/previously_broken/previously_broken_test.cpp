// previously_broken — a regression suite of programs/patterns that ONCE broke the
// cheatah compiler or runtime. It is run FIRST in the QA gate (CTest label
// "previously_broken") so a reintroduced regression fails fast, before the rest of the
// suite. Each test names the bug it pins. Uses the shared purrc→cheatah compile-run
// harness (tests/purrc/e2e_harness.hpp).
#include <cstdlib>
#include <fstream>
#include <string>

#include "e2e_harness.hpp"

#ifndef PREVBROKEN_DIR
#define PREVBROKEN_DIR "."
#endif

// Bug: a call argument / list literal split across multiple lines failed to parse with
// "expected an expression" — the lexer emitted a significant Newline after `(` and inside
// `[ ]`. Fixed by making newlines insignificant inside `( )` / `[ ]` (implicit line
// continuation). This runs the exact (corrected) program from review/c1.purr.
TEST(PreviouslyBroken, MultilineArrayInCallArg) {
    int rc = -1;
    const std::string out = e2e::run_purr_file(
        "prevbroken_c1", std::string(PREVBROKEN_DIR) + "/multiline_array.purr", rc);
    EXPECT_EQ(rc, 0) << "multiline_array.purr failed to compile or run";
    EXPECT_EQ(out, "ok\n");
}

// Bug: bare `str(...)` failed to compile ("use of undeclared identifier 'str'") because str
// lived only in the `io` module, not in the always-available `builtins`. Fixed by adding
// `str()` to builtins (the codegen builtin map + a Streamable str + a bool overload). Runs
// the user's Python-style timing loop; the elapsed nanoseconds are timing-dependent, so we
// assert structure (100 iterations + a final "Took:" line), not an exact byte stream.
TEST(PreviouslyBroken, BareStrBuiltinInTimingLoop) {
    int rc = -1;
    const std::string out = e2e::run_purr_file(
        "prevbroken_timing_str", std::string(PREVBROKEN_DIR) + "/timing_str.purr", rc);
    EXPECT_EQ(rc, 0) << "timing_str.purr failed to compile or run";
    std::size_t n = 0, pos = 0;
    while ((pos = out.find("stupid stupid stupid", pos)) != std::string::npos) {
        ++n;
        pos += 1;
    }
    EXPECT_EQ(n, 100u) << "expected 100 loop iterations\n" << out;
    EXPECT_NE(out.find("Took: "), std::string::npos) << "expected a 'Took: <ns>' line\n" << out;
}

// str() is idempotent: `str(str(a))` must reduce to a single `str(a)` — no redundant
// intermediary in the emitted C++ (cheatah reduces to the most minimal C++).
TEST(PreviouslyBroken, StrIsIdempotent) {
    const std::string gen = e2e::expect_e2e_source("prevbroken_str_idem", R"PURR(import io
let a = 7
io.print(str(str(a)))
)PURR",
                                                    "7\n");
    EXPECT_NE(gen.find("str(a)"), std::string::npos) << gen;
    EXPECT_EQ(gen.find("str(builtins::str"), std::string::npos)
        << "no nested str(str(...)) should survive\n"
        << gen;
}

// `"took: " + a` auto-stringifies the non-string operand and must emit the SAME minimal C++
// as the explicit `"took: " + str(a)` — str() inserted only where needed, identical output.
TEST(PreviouslyBroken, StringConcatAutoStringifiesIdenticallyToStr) {
    const std::string needle = "std::string(\"took: \") + builtins::str(a)";
    const std::string g1 = e2e::expect_e2e_source("prevbroken_concat_explicit", R"PURR(import io
let a = 42
io.print("took: " + str(a))
)PURR",
                                                   "took: 42\n");
    const std::string g2 = e2e::expect_e2e_source("prevbroken_concat_auto", R"PURR(import io
let a = 42
io.print("took: " + a)
)PURR",
                                                   "took: 42\n");
    EXPECT_NE(g1.find(needle), std::string::npos) << "explicit str form\n" << g1;
    EXPECT_NE(g2.find(needle), std::string::npos) << "auto-stringified form\n" << g2;
}

// SYSTEM-LEVEL: a `let` whose variable is never used/printed/returned, in a function that is
// NOT an exported return path, is removed — but the side-effecting call is kept and a RETURNED
// local survives. The program still runs identically. (Mirrors the user's review/c1.purr.)
TEST(PreviouslyBroken, RemovesUnusedUnexportedVariable) {
    const std::string gen = e2e::expect_e2e_source("prevbroken_dce", R"PURR(import io
fn make() {
    let kept = 42
    return kept
}
fn run_once() {
    let dead = make()
}
run_once()
io.print("ok")
)PURR",
                                                    "ok\n");
    EXPECT_NE(gen.find("auto kept = 42LL;"), std::string::npos) << "returned local kept\n" << gen;
    EXPECT_NE(gen.find("return kept;"), std::string::npos) << gen;
    EXPECT_EQ(gen.find("auto dead ="), std::string::npos)
        << "unused, unexported local should be removed\n"
        << gen;
    EXPECT_NE(gen.find("    make();"), std::string::npos)
        << "the side-effecting call must be preserved\n"
        << gen;
}

// SYSTEM-LEVEL on the user's exact example file: the loop's `let A = solve_system()` (A unused,
// solve_system not an exported return path) drops A to a bare call; solve_system's own `return A`
// keeps that A; the program still prints "ok".
TEST(PreviouslyBroken, MultilineExampleRemovesUnusedLoopVariable) {
    int rc = -1;
    const std::string out = e2e::run_purr_file(
        "prevbroken_ma_dce", std::string(PREVBROKEN_DIR) + "/multiline_array.purr", rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out, "ok\n");
    const std::string gen =
        e2e::read_file(std::string(PURR_TEST_TMP) + "/prevbroken_ma_dce_prog.so.gen.cpp");
    EXPECT_NE(gen.find("solve_system();"), std::string::npos) << "unused A -> bare call\n" << gen;
    EXPECT_EQ(gen.find("auto A = solve_system();"), std::string::npos) << "A removed\n" << gen;
    EXPECT_NE(gen.find("return A;"), std::string::npos)
        << "returned A inside solve_system is kept\n"
        << gen;
}

// Optimizations are ON by default in purrc, and basic dead-variable removal MUST happen:
// this is a performance language, so NOT optimizing when optimizations are on is itself a
// bug. Same program/assertion as NoOptimizeCppKeepsUnusedVariables below, but with the
// DEFAULT (no opt-out flag) — the unused local must be gone from the generated C++.
TEST(PreviouslyBroken, OptimizationsOnRemoveUnusedVariables) {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/prevbroken_opt_on.purr";
    const std::string mod = tmp + "/prevbroken_opt_on.so";
    { std::ofstream f(purr); f << "import io\nlet unused = 5\nio.print(\"ok\")\n"; }
    const std::string cmd = std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "purrc (optimizations on) failed";
    const std::string gen = e2e::read_file(mod + ".gen.cpp");
    EXPECT_EQ(gen.find("unused"), std::string::npos)
        << "with optimizations ON (the default), the unused variable must be removed\n"
        << gen;
}

// The --no-optimize-cpp umbrella (and --no-remove-variables) keep unused locals verbatim.
TEST(PreviouslyBroken, NoOptimizeCppKeepsUnusedVariables) {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/prevbroken_noopt.purr";
    const std::string mod = tmp + "/prevbroken_noopt.so";
    { std::ofstream f(purr); f << "import io\nlet unused = 5\nio.print(\"ok\")\n"; }
    const std::string cmd =
        std::string(PURRC_PATH) + " --no-optimize-cpp \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "purrc --no-optimize-cpp failed";
    const std::string gen = e2e::read_file(mod + ".gen.cpp");
    EXPECT_NE(gen.find("auto unused = 5LL;"), std::string::npos)
        << "unused local should be kept verbatim\n"
        << gen;
}

// Bug: C++20-style designated-initializer struct construction `Type({.field = value})` did
// not parse, and structs were not printable. Now it lowers to a C++20 designated initializer
// and a streamable struct gets an auto operator<<. (The user's review/struct_init_test.purr.)
TEST(PreviouslyBroken, StructDesignatedInitAndPrinting) {
    int rc = -1;
    const std::string out = e2e::run_purr_file(
        "prevbroken_struct_init", std::string(PREVBROKEN_DIR) + "/struct_init.purr", rc);
    EXPECT_EQ(rc, 0) << "struct_init.purr failed to compile or run";
    // io.print -> PRETTY multi-line; io.rprint -> compact; then two field accesses.
    EXPECT_EQ(out,
              "DumbDumb(\n    what = 10,\n    what2 = 300.2\n)\n"  // io.print(d)  — pretty
              "DumbDumb(what=10, what2=300.2)\n"                   // io.rprint(d) — compact
              "10\n"                                               // io.print(d.what)
              "300.2\n");                                          // io.print(d.what2)
    const std::string gen =
        e2e::read_file(std::string(PURR_TEST_TMP) + "/prevbroken_struct_init_prog.so.gen.cpp");
    // Lowers to a C++20 designated initializer (narrowing-safe int->float), and — because the
    // source spanned multiple lines — the generated C++ stays multi-line too.
    EXPECT_NE(gen.find("DumbDumb{\n"), std::string::npos) << "multi-line init preserved\n" << gen;
    EXPECT_NE(gen.find(".what = static_cast<double>(10LL)"), std::string::npos) << gen;
    EXPECT_NE(gen.find("std::ostream& operator<<"), std::string::npos)
        << "streamable struct gets an auto operator<<\n"
        << gen;
}

// SYSTEM-LEVEL safety: a designated initializer that omits a field DEFAULT-initializes it
// (zero) rather than leaving garbage — an unset value is a bug, never random memory.
TEST(PreviouslyBroken, StructDesignatedInitDefaultInitializesOmittedFields) {
    e2e::expect_e2e("prevbroken_struct_partial", R"PURR(import io
struct P { a: float
 b: float }
let p = P({.a = 5})
io.rprint(p)
)PURR",
                    "P(a=5, b=0)\n");  // rprint: compact form; the point is b defaulted to 0
}

// SYSTEM-LEVEL multi-line regression guard: every construct that supports it must keep the
// source's multi-line layout in the generated C++ — a regression makes .gen.cpp unreadable.
// Covers a call argument broken across lines and a struct designated initializer (list/dict
// literals are covered by the tests below). Full purrc pipeline, asserting on emitted C++.
TEST(PreviouslyBroken, MultilineCallArgStaysMultiline) {
    // `m` is unused, so DCE drops the binding — but the multi-line call it keeps must stay
    // multi-line.
    const std::string gen = e2e::expect_e2e_source("prevbroken_ml_callarg", R"PURR(import io
import ndarray
let m = ndarray.array(
    [
        [1.0, 2.0],
        [3.0, 4.0]
    ])
io.print("ok")
)PURR",
                                                    "ok\n");
    EXPECT_NE(gen.find("ndarray::array(std::vector{\n"), std::string::npos)
        << "a multi-line array call argument must stay multi-line\n"
        << gen;
}

TEST(PreviouslyBroken, MultilineStructInitStaysMultiline) {
    const std::string gen = e2e::expect_e2e_source("prevbroken_ml_structinit", R"PURR(import io
struct V { x: float
 y: float }
let v = V(
    {
        .x = 1,
        .y = 2
    })
io.rprint(v)
)PURR",
                                                    "V(x=1, y=2)\n");  // point is the multi-line .gen.cpp
    EXPECT_NE(gen.find("V{\n"), std::string::npos)
        << "a multi-line struct designated initializer must stay multi-line\n"
        << gen;
    EXPECT_NE(gen.find("        .x = static_cast<double>(1LL),\n"), std::string::npos) << gen;
}

// `let x` with NO initializer is allowed: it is realized (`auto x = …`) at its first
// assignment, so the variable comes into being exactly where it is first given a value.
TEST(PreviouslyBroken, LetWithoutValueRealizedAtFirstAssignment) {
    const std::string gen = e2e::expect_e2e_source("prevbroken_letnoval", R"PURR(import io
let x
x = 5
io.print(x)
)PURR",
                                                    "5\n");
    EXPECT_NE(gen.find("auto x = 5LL;"), std::string::npos)
        << "the no-value let is realized at its first assignment\n"
        << gen;
}

// A no-value let that is never assigned AND never used is simply removed (zero possibility of
// ever being used) — no uninitialized variable is emitted.
TEST(PreviouslyBroken, LetWithoutValueUnusedIsRemoved) {
    const std::string gen = e2e::expect_e2e_source("prevbroken_letnoval_unused", R"PURR(import io
let x
io.print("ok")
)PURR",
                                                    "ok\n");
    EXPECT_EQ(gen.find("auto x"), std::string::npos)
        << "an unused, never-assigned let must not be emitted\n"
        << gen;
}

// Using a variable that is never given a value must NOT compile — an unset variable is a bug.
TEST(PreviouslyBroken, UnsetVariableUsedFailsToCompile) {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/prevbroken_unset.purr";
    const std::string mod = tmp + "/prevbroken_unset.so";
    { std::ofstream f(purr); f << "import io\nlet x\nio.print(x)\n"; }
    const std::string cmd =
        std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\" >/dev/null 2>&1";
    EXPECT_NE(std::system(cmd.c_str()), 0)
        << "reading a never-assigned variable must fail to compile";
}

// The generated C++ must PRESERVE the source's multi-line layout: a multi-line array
// literal stays multi-line (each element on its own indented line) so .gen.cpp is readable
// rather than collapsed onto one unwieldy line. The inner single-line rows stay on one line.
TEST(PreviouslyBroken, MultilineLiteralStaysMultilineInCodegen) {
    const std::string gen = e2e::expect_e2e_source("prevbroken_ml_let", R"PURR(import io
let m = [
    [1, 2],
    [3, 4]
]
io.print(m[0][1])
)PURR",
                                                   "2\n");
    EXPECT_NE(gen.find("std::vector{\n"), std::string::npos)
        << "outer literal should break across lines\n"
        << gen;
    EXPECT_NE(gen.find("        std::vector{1LL, 2LL},\n"), std::string::npos)
        << "inner single-line row should stay on one indented line\n"
        << gen;
}

// A single-line literal must STAY single-line (no spurious reformatting from the new path).
TEST(PreviouslyBroken, SingleLineLiteralStaysSingleLine) {
    const std::string gen = e2e::expect_e2e_source("prevbroken_sl", R"PURR(import io
let v = [1, 2, 3]
io.print(v[2])
)PURR",
                                                   "3\n");
    EXPECT_NE(gen.find("std::vector{1LL, 2LL, 3LL}"), std::string::npos) << gen;
}

// A multi-line DICT literal also stays multi-line in the generated C++.
TEST(PreviouslyBroken, MultilineDictStaysMultilineInCodegen) {
    const std::string gen = e2e::expect_e2e_source("prevbroken_dict", R"PURR(import io
let d = {
    "a": 1,
    "b": 2
}
io.print(d["a"])
)PURR",
                                                   "1\n");
    EXPECT_NE(gen.find("std::unordered_map{\n"), std::string::npos) << gen;
}
