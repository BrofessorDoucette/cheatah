// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level "real program" test for the `builtins` module: a single cohesive
// program that exercises EVERY public, purr-callable built-in declared in
// stdlib/builtins/builtins.hpp in one run, then asserts its stdout byte-for-byte.
//
// Unlike the per-function compile-run suite (builtins_cr_test.cpp, one built-in
// each), this is one small but genuine program: it inspects a string with
// len/ord/chr, renders a number in every base (hex/oct/bin), shows its printable
// repr with ascii, and runs the int()/float()/bool() conversions (the Python
// spellings map to builtins::to_int/to_float/to_bool). The program is fully
// DETERMINISTIC, so its output is asserted exactly.
//
// Coverage of stdlib/builtins/builtins.hpp (every purr-callable built-in):
//   len, ord, chr, hex, oct, bin, ascii,
//   int()  -> to_int  (from string and from float),
//   float()-> to_float(from string and from int),
//   bool() -> to_bool (from string and from number),
//   hash   -> value is implementation-defined, so we assert only the
//             deterministic property hash("a") == hash("a") (not the value).
//
// Note: purrc treats a newline as a statement terminator, so each call stays on
// its own source line.
#include "e2e_harness.hpp"

TEST(StdlibE2E, Builtins) {
    e2e::expect_e2e("builtins_sys", R"PURR(import io
import builtins

# Inspect a string byte-by-byte using len/ord/chr (+ ascii for its repr).
let s = "Cat"
io.print(io.format("len({}) = {}", ascii(s), len(s)))

let i = 0
while i < len(s) {
    let ch = chr(ord(s) + i)
    io.print(io.format("ord/chr step {}: {}", i, ch))
    i = i + 1
}

# Base representations of a number: hex / oct / bin.
let n = 255
io.print(io.format("{} -> hex={} oct={} bin={}", n, hex(n), oct(n), bin(n)))

# Conversions: int()/float()/bool() map to to_int/to_float/to_bool.
io.print(io.format("int(\"42\")={} int(3.9)={}", int("42"), int(3.9)))
io.print(io.format("float(\"2.5\")={} float(7)={}", float("2.5"), float(7)))
io.print(io.format("bool(\"x\")={} bool(0)={} bool(7)={}", bool("x"), bool(0), bool(7)))

# hash: value is implementation-defined; assert only a deterministic property.
io.print(io.format("hash stable: {}", bool(hash("a") == hash("a"))))
)PURR",
        "len('Cat') = 3\n"
        "ord/chr step 0: C\n"
        "ord/chr step 1: D\n"
        "ord/chr step 2: E\n"
        "255 -> hex=0xff oct=0o377 bin=0b11111111\n"
        "int(\"42\")=42 int(3.9)=3\n"
        "float(\"2.5\")=2.5 float(7)=7\n"
        "bool(\"x\")=1 bool(0)=0 bool(7)=1\n"
        "hash stable: 1\n");
}
