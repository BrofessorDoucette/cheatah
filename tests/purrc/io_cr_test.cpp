// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Compile-run unit tests for the `io` module: one test per purr-callable function.
// Each writes a tiny .purr that exercises a single io entry point, compiles it with
// purrc, runs it under the cheatah runtime, and asserts the exact stdout. Complements
// the in-process unit tests (stdlib/tests/io_test.cpp) and the per-module
// system-level test (StdlibE2E.Io).
//
// File I/O tests write to a unique path under /tmp from inside the .purr program, then
// read it back and io.print the content, so each test is self-contained and
// deterministic. io.input is intentionally NOT covered here: it reads from stdin, which
// the e2e harness does not feed (it is covered by the in-process test
// CheatahIo.InputReadsALine instead).
#include "e2e_harness.hpp"

TEST(IoCompileRun, Str) {
    e2e::expect_e2e("io_str", R"PURR(import io
io.print(io.str(42))
)PURR", "42\n");
}

TEST(IoCompileRun, Repr) {
    e2e::expect_e2e("io_repr", R"PURR(import io
io.print(io.repr("hi"))
)PURR", "'hi'\n");
}

TEST(IoCompileRun, Print) {
    e2e::expect_e2e("io_print", R"PURR(import io
io.print("meow", 42, "purr")
)PURR", "meow 42 purr\n");
}

// io.print renders a struct PRETTY (nice + readable by default): on multiple indented lines.
TEST(IoCompileRun, PrintPrettyStruct) {
    e2e::expect_e2e("io_print_pretty", R"PURR(import io
struct Point {
    x: float
    y: float
}
let p = Point({.x = 1, .y = 2})
io.print(p)
)PURR",
                    "Point(\n    x = 1,\n    y = 2\n)\n");
}

// io.rprint prints a struct RAW — its compact `Name(field=value, …)` form, exactly as stored.
TEST(IoCompileRun, Rprint) {
    e2e::expect_e2e("io_rprint", R"PURR(import io
struct Point {
    x: float
    y: float
}
let p = Point({.x = 1, .y = 2})
io.rprint(p)
)PURR",
                    "Point(x=1, y=2)\n");
}

// Nested structs pretty-print recursively, each level indented under its parent.
TEST(IoCompileRun, PrintPrettyNestedStruct) {
    e2e::expect_e2e("io_print_pretty_nested", R"PURR(import io
struct Inner { a: float }
struct Outer {
    inner: Inner
    b: float
}
let o = Outer({.inner = Inner({.a = 5}), .b = 7})
io.print(o)
)PURR",
                    "Outer(\n    inner = Inner(\n        a = 5\n    ),\n    b = 7\n)\n");
}

TEST(IoCompileRun, Format) {
    e2e::expect_e2e("io_format", R"PURR(import io
io.print(io.format("{} ate {} fish", "cat", 3))
)PURR", "cat ate 3 fish\n");
}

TEST(IoCompileRun, Fixed) {
    e2e::expect_e2e("io_fixed", R"PURR(import io
io.print(io.fixed(2.675, 2), io.fixed(12.0, 1), io.fixed(1.5, 0), io.fixed(-1.25, 1))
)PURR", "2.67 12.0 2 -1.2\n");
}

TEST(IoCompileRun, OpenWriteRead) {
    e2e::expect_e2e("io_open", R"PURR(import io
let f = io.open("/tmp/cr_io_open.txt", "w")
f.write("meow\npurr\n")
f.close()
let g = io.open("/tmp/cr_io_open.txt", "r")
io.print(g.read())
g.close()
)PURR", "meow\npurr\n\n");
}

TEST(IoCompileRun, Readline) {
    e2e::expect_e2e("io_readline", R"PURR(import io
let f = io.open("/tmp/cr_io_readline.txt", "w")
f.write("meow\npurr\nnap\n")
f.close()
let g = io.open("/tmp/cr_io_readline.txt", "r")
io.print(g.readline())
io.print(g.readline())
g.close()
)PURR", "meow\npurr\n");
}

TEST(IoCompileRun, Readlines) {
    e2e::expect_e2e("io_readlines", R"PURR(import io
let f = io.open("/tmp/cr_io_readlines.txt", "w")
f.write("meow\npurr\nnap\n")
f.close()
let g = io.open("/tmp/cr_io_readlines.txt", "r")
let lines = g.readlines()
g.close()
for line in lines { io.print(line) }
)PURR", "meow\npurr\nnap\n");
}

TEST(IoCompileRun, IsOpenAndClose) {
    e2e::expect_e2e("io_is_open", R"PURR(import io
let f = io.open("/tmp/cr_io_isopen.txt", "w")
io.print(f.is_open())
f.write("x")
f.close()
io.print(f.is_open())
)PURR", "True\nFalse\n");
}

TEST(IoCompileRun, ReadFile) {
    e2e::expect_e2e("io_read_file", R"PURR(import io
let f = io.open("/tmp/cr_io_readfile.txt", "w")
f.write("meow\npurr\n")
f.close()
io.print(io.read_file("/tmp/cr_io_readfile.txt"))
)PURR", "meow\npurr\n\n");
}
