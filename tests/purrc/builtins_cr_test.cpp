// Compile-run unit tests for the `builtins` module: one test per built-in. Each
// writes a tiny .purr that calls a single built-in (built-ins need no `import`
// and take no module prefix, e.g. `len("hi")`), compiles it with purrc, runs it
// under the cheatah runtime, and asserts the exact stdout. Complements the
// in-process unit tests (stdlib/tests/builtins_test.cpp).
//
// Note: the keyword-named conversions are spelled with their Python names in
// cheatah source (`int`/`float`/`bool`), which the compiler maps to
// builtins::to_int/to_float/to_bool. The `hash` built-in is intentionally not
// covered here: its value is implementation-defined and not stable across
// compilers/libstdc++ versions, so it has no portable expected stdout.
#include "e2e_harness.hpp"

TEST(BuiltinsCompileRun, Len) {
    e2e::expect_e2e("builtins_len", R"PURR(import io
io.print(len("meow"))
)PURR", "4\n");
}

TEST(BuiltinsCompileRun, Ord) {
    e2e::expect_e2e("builtins_ord", R"PURR(import io
io.print(ord("A"))
)PURR", "65\n");
}

TEST(BuiltinsCompileRun, Str) {
    // Bare str() (no import, no module prefix): int, float, bool, and inside a concat where
    // it is redundant ("n=" + 7 auto-stringifies to the same thing).
    e2e::expect_e2e("builtins_str", R"PURR(import io
io.print(str(42))
io.print(str(3.14))
io.print(str(true))
io.print("n=" + str(7))
)PURR", "42\n3.14\nTrue\nn=7\n");
}

TEST(BuiltinsCompileRun, Chr) {
    e2e::expect_e2e("builtins_chr", R"PURR(import io
io.print(chr(65))
)PURR", "A\n");
}

TEST(BuiltinsCompileRun, Hex) {
    e2e::expect_e2e("builtins_hex", R"PURR(import io
io.print(hex(255))
)PURR", "0xff\n");
}

TEST(BuiltinsCompileRun, Oct) {
    e2e::expect_e2e("builtins_oct", R"PURR(import io
io.print(oct(8))
)PURR", "0o10\n");
}

TEST(BuiltinsCompileRun, Bin) {
    e2e::expect_e2e("builtins_bin", R"PURR(import io
io.print(bin(5))
)PURR", "0b101\n");
}

TEST(BuiltinsCompileRun, Ascii) {
    e2e::expect_e2e("builtins_ascii", R"PURR(import io
io.print(ascii("hi"))
)PURR", "'hi'\n");
}

TEST(BuiltinsCompileRun, IntFromString) {
    e2e::expect_e2e("builtins_int_str", R"PURR(import io
io.print(int("42"))
)PURR", "42\n");
}

TEST(BuiltinsCompileRun, IntFromFloat) {
    e2e::expect_e2e("builtins_int_float", R"PURR(import io
io.print(int(3.9))
)PURR", "3\n");
}

TEST(BuiltinsCompileRun, FloatFromString) {
    e2e::expect_e2e("builtins_float_str", R"PURR(import io
io.print(float("2.5"))
)PURR", "2.5\n");
}

TEST(BuiltinsCompileRun, FloatFromInt) {
    e2e::expect_e2e("builtins_float_int", R"PURR(import io
io.print(float(7))
)PURR", "7\n");
}

TEST(BuiltinsCompileRun, BoolFromString) {
    e2e::expect_e2e("builtins_bool_str", R"PURR(import io
io.print(bool("x"))
)PURR", "True\n");
}

TEST(BuiltinsCompileRun, BoolFromZero) {
    e2e::expect_e2e("builtins_bool_zero", R"PURR(import io
io.print(bool(0))
)PURR", "False\n");
}

TEST(BuiltinsCompileRun, BoolFromNonzero) {
    e2e::expect_e2e("builtins_bool_nonzero", R"PURR(import io
io.print(bool(7))
)PURR", "True\n");
}

TEST(BuiltinsCompileRun, TrueDivision) {
    e2e::expect_e2e("builtins_truediv", R"PURR(import io
io.print(6 / 4)
io.print(6 / 2)
)PURR", "1.5\n3\n");
}

TEST(BuiltinsCompileRun, FloorDivision) {
    e2e::expect_e2e("builtins_floordiv", R"PURR(import io
io.print(7 // 2)
io.print(-7 // 2)
)PURR", "3\n-4\n");
}
