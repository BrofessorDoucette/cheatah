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

TEST(IoCompileRun, Format) {
    e2e::expect_e2e("io_format", R"PURR(import io
io.print(io.format("{} ate {} fish", "cat", 3))
)PURR", "cat ate 3 fish\n");
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
