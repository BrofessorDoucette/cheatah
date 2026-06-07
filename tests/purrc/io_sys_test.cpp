// System-level (whole-module) test for the `io` stdlib module: ONE cohesive .purr
// program that exercises EVERY public purr-callable entry point in stdlib/io/io.hpp
// in a single run, compiles it with purrc, runs it under the cheatah runtime, and
// asserts the exact stdout byte-for-byte. Complements the per-function compile-run
// tests (tests/purrc/io_cr_test.cpp) and the in-process unit tests
// (stdlib/tests/io_test.cpp).
//
// Coverage (every documented purr-callable symbol in io.hpp):
//   io.str (generic Streamable overload)  -> io.str(3), io.str(os.remove(...))
//   io.str (bool overload)                -> io.str(1 == 1) / io.str(1 == 2) -> True/False
//   io.str (std::string identity)         -> io.str(name)
//   io.repr (std::string overload)        -> io.repr(name), io.repr(first), io.repr(rest)
//   io.repr (const char* overload)        -> io.repr("raw")
//   io.print                              -> used throughout
//   io.format                             -> summary + the file body
//   io.open (free function)               -> let f/g/h = io.open(path, mode)
//   File.write                            -> f.write(...)
//   File.is_open                          -> f.is_open() before/after close
//   File.close                            -> f.close()
//   File.read                             -> g.read()
//   File.readline                         -> g.readline()
//   File.readlines                        -> h.readlines()
//   io.read_file                          -> io.read_file(path)
//
// io.input is intentionally NOT covered: it reads from stdin, which the e2e harness
// does not feed (covered instead by the in-process test CheatahIo.InputReadsALine).
//
// The program builds strings with str/repr/format, writes a temp file under /tmp via
// the File object, reads it back three ways (readline+read, readlines, read_file),
// prints a deterministic summary, then deletes the temp file (os.remove) and confirms
// it is gone (os.path.exists). os is imported only for path joining and cleanup.
#include "e2e_harness.hpp"

TEST(StdlibE2E, Io) {
    e2e::expect_e2e("io_sys", R"PURR(import io
import os

let path = os.path.join("/tmp", "io_sys_data.txt")

let name = "lines"
let header = io.str(1 == 1)
let count = io.str(3)
let kind = io.str(name)
let label = io.repr(name)
let lit = io.repr("raw")
let summary = io.format("{} {} {} ({} total)", header, label, lit, count)

let f = io.open(path, "w")
f.write(io.format("alpha={}\n", 1))
f.write(io.format("beta={}\n", io.str(1 == 2)))
f.write("gamma=3\n")
io.print("writing open:", io.str(f.is_open()))
f.close()
io.print("writing closed:", io.str(f.is_open()))

let g = io.open(path, "r")
let first = g.readline()
let rest = g.read()
g.close()

let h = io.open(path, "r")
let all_lines = h.readlines()
h.close()

let whole = io.read_file(path)

io.print("kind:", kind)
io.print("summary:", summary)
io.print("first:", io.repr(first))
io.print("rest:", io.repr(rest))
io.print("line count:", io.str(3))
for line in all_lines { io.print("  line:", line) }
io.print("first line again:", all_lines[0])
io.print("read_file whole:")
io.print(whole)

io.print("removed:", io.str(os.remove(path)))
io.print("still exists:", io.str(os.path.exists(path)))
)PURR",
        "writing open: True\n"
        "writing closed: False\n"
        "kind: lines\n"
        "summary: True 'lines' 'raw' (3 total)\n"
        "first: 'alpha=1'\n"
        "rest: 'beta=False\ngamma=3\n'\n"
        "line count: 3\n"
        "  line: alpha=1\n"
        "  line: beta=False\n"
        "  line: gamma=3\n"
        "first line again: alpha=1\n"
        "read_file whole:\n"
        "alpha=1\nbeta=False\ngamma=3\n\n"
        "removed: True\n"
        "still exists: False\n");
}
