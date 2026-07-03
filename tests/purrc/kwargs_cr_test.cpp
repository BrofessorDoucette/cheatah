// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Compile-run system tests for DEFAULT and KEYWORD arguments:
//   fn f(a, b = expr) { ... }     — trailing defaults (lower to forwarding overloads, since a
//                                    C++ default on an `auto` parameter cannot drive deduction);
//   f(x, b = v) / f(b = v, a = u) — keyword arguments, reordered at the call site against the
//                                    known signature, missing slots filled from the defaults.
// Each test compiles a .purr with purrc, runs it, and asserts exact stdout; the negative tests
// assert that purrc REJECTS the program (bad kwarg name, non-trailing default, dup parameter).
#include "e2e_harness.hpp"

TEST(KwargsCompileRun, TrailingDefaults) {
    e2e::expect_e2e("kwargs_defaults", R"PURR(import io
fn greet(name, greeting = "hello", punct = "!") {
    io.print(greeting + ", " + name + punct)
}
greet("world")
greet("cheatah", "purr")
)PURR", "hello, world!\npurr, cheatah!\n");
}

TEST(KwargsCompileRun, KeywordReorder) {
    e2e::expect_e2e("kwargs_reorder", R"PURR(import io
fn greet(name, greeting = "hello", punct = "!") {
    io.print(greeting + ", " + name + punct)
}
greet("kwargs", punct = "?")
greet(punct = ".", name = "reordered")
greet("mixed", greeting = "hi", punct = "~")
)PURR", "hello, kwargs?\nhello, reordered.\nhi, mixed~\n");
}

TEST(KwargsCompileRun, DefaultsInExpressions) {
    e2e::expect_e2e("kwargs_exprs", R"PURR(import io
fn area(w, h = 2) { return w * h }
io.print(area(3))
io.print(area(3, h = 5))
io.print(area(h = 4, w = 2))
)PURR", "6\n15\n8\n");
}

// purrc must REJECT: a keyword that names no parameter.
TEST(KwargsCompileRun, RejectsUnknownKeyword) {
    e2e::expect_compile_fail("kwargs_unknown", R"PURR(import io
fn f(a) { io.print(a) }
f(b = 1)
)PURR");
}

// purrc must REJECT: a non-defaulted parameter after a defaulted one.
TEST(KwargsCompileRun, RejectsNonTrailingDefault) {
    e2e::expect_compile_fail("kwargs_nontrailing", R"PURR(import io
fn f(a = 1, b) { io.print(a + b) }
f(1, 2)
)PURR");
}

// purrc must REJECT: the same parameter given positionally AND by keyword.
TEST(KwargsCompileRun, RejectsDuplicateParameter) {
    e2e::expect_compile_fail("kwargs_dup", R"PURR(import io
fn f(a, b = 2) { io.print(a + b) }
f(1, a = 3)
)PURR");
}

// ---- `%` (Python floor-mod) and membership `in` (added alongside the requests port) ----

TEST(LangFeatures, Modulo) {
    e2e::expect_e2e("lang_modulo", R"PURR(import io
io.print(7 % 3)
io.print(-7 % 3)
io.print(7.5 % 2)
io.print(255 % 16)
)PURR", "1\n2\n1.5\n15\n");
}

TEST(LangFeatures, InOperator) {
    e2e::expect_e2e("lang_in", R"PURR(import io
let d: dict<str, int> = {}
d["alpha"] = 1
io.print("alpha" in d)
io.print("beta" in d)
let xs = [10, 20, 30]
io.print(20 in xs)
io.print(99 in xs)
io.print("ell" in "hello")
)PURR", "True\nFalse\nTrue\nFalse\nTrue\n");
}
