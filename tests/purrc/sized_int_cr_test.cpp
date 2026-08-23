// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Compile-run tests for OPT-IN sized integer storage types (the memory-footprint feature).
//
// A declaration may opt into an explicit width — `i8`/`u32`/… (abbreviated) or `int8`/`uint32`/…
// (full spelling), both the SAME <cstdint> exact-width type by construction — anywhere a type
// annotation appears: scalars, `list`/`dict`/`array` elements, `ndarray` elements, and struct
// fields. The stored form shrinks (proven with the in-language `sizeof` builtin); arithmetic still
// promotes, and `int` itself is UNCHANGED (still 64-bit) so standalone integers never regress.
//
// Each test asserts BOTH the emitted C++ (the declared std::intN_t really reaches storage) and the
// program's stdout (numeric — never a char, the i8/u8 gotcha these types must not fall into).
#include "e2e_harness.hpp"

using e2e::expect_e2e;
using e2e::expect_e2e_source;
using e2e::expect_compile_fail;

// A scalar width, BOTH spellings — the abbreviated and full names deduce to one canonical type.
TEST(SizedIntCompileRun, ScalarBothSpellings) {
    std::string src = expect_e2e_source("sized_scalar", R"PURR(import io
let a: i8 = 5
let b: int8 = -7
let c: u32 = 4000000000
let d: uint32 = 42
io.print(a, b, c, d)
)PURR", "5 -7 4000000000 42\n");
    EXPECT_NE(src.find("std::int8_t a = 5LL"), std::string::npos) << src;
    EXPECT_NE(src.find("std::int8_t b = (-7LL)"), std::string::npos) << src;   // int8 == i8
    EXPECT_NE(src.find("std::uint32_t c = 4000000000LL"), std::string::npos) << src;
    EXPECT_NE(src.find("std::uint32_t d = 42LL"), std::string::npos) << src;   // uint32 == u32
}

// The ORIGINAL <cstdint> spelling (`int16_t`/`uint8_t`) names the SAME type as `i16`/`u8` — a C
// programmer writes the name they already know, with no difference in the emitted storage.
TEST(SizedIntCompileRun, CstdintSpelling) {
    std::string src = expect_e2e_source("sized_cstdint", R"PURR(import io
let a: int16_t = 300
let b: uint8_t = 200
let xs: list<int32_t> = [1, 2, 3]
io.print(a, b)
io.print(xs)
io.print(sizeof(a), sizeof(b))
)PURR", "300 200\n[1, 2, 3]\n2 1\n");
    EXPECT_NE(src.find("std::int16_t a = 300LL"), std::string::npos) << src;   // int16_t == i16
    EXPECT_NE(src.find("std::uint8_t b = 200LL"), std::string::npos) << src;   // uint8_t == u8
    EXPECT_NE(src.find("std::vector<std::int32_t> xs"), std::string::npos) << src;
}

// A narrow list literal must be BUILT AS the declared vector, not CTAD'd to vector<long long>
// (which would not assign). Elements print numerically even for the u8 char-sized case.
TEST(SizedIntCompileRun, ListNarrow) {
    std::string src = expect_e2e_source("sized_list", R"PURR(import io
let xs: list<i32> = [1, 2, 3]
let bs: list<u8> = [250, 4, 9]
io.print(xs)
io.print(bs)
)PURR", "[1, 2, 3]\n[250, 4, 9]\n");
    EXPECT_NE(src.find("std::vector<std::int32_t> xs = std::vector<std::int32_t>{1LL, 2LL, 3LL}"),
              std::string::npos) << src;
    EXPECT_NE(src.find("std::vector<std::uint8_t> bs = std::vector<std::uint8_t>{250LL, 4LL, 9LL}"),
              std::string::npos) << src;
}

// A narrow dict — the value width drives the map type; a constant value narrows into it.
TEST(SizedIntCompileRun, DictNarrow) {
    std::string src = expect_e2e_source("sized_dict", R"PURR(import io
let m: dict<str, u8> = {"a": 1}
io.print(m["a"])
)PURR", "1\n");
    EXPECT_NE(src.find("std::unordered_map<std::string, std::uint8_t>"), std::string::npos) << src;
}

// A fixed-size array of a narrow element.
TEST(SizedIntCompileRun, ArrayNarrow) {
    std::string src = expect_e2e_source("sized_array", R"PURR(import io
let a: array<i16, 3> = [10, 20, 30]
io.print(a[2])
)PURR", "30\n");
    EXPECT_NE(src.find("std::array<std::int16_t, 3>"), std::string::npos) << src;
}

// A narrow-element ndarray is now CONSTRUCTIBLE: a declared `ndarray<i16>` drives the element type
// (the initializer is converted for you), and the param reference / return type carry the width.
TEST(SizedIntCompileRun, NdarrayNarrowElement) {
    std::string src = expect_e2e_source("sized_ndarray", R"PURR(import io
import ndarray
fn first(a: ndarray<i16>) -> i16 {
    return 0
}
fn main() {
    let v: ndarray<i16> = ndarray.array([1, 2, 3])
    io.print(v)
    io.print(first(v))
}
main()
)PURR", "[1, 2, 3]\n0\n");
    EXPECT_NE(src.find("basic_ndarray<std::int16_t>& a"), std::string::npos) << src;  // param ref
    EXPECT_NE(src.find("std::int16_t first("), std::string::npos) << src;             // return width
    EXPECT_NE(src.find("astype<std::int16_t>"), std::string::npos) << src;            // driven construction
}

// A `-> list<i8>` return of a bare literal is built AS the declared narrow vector.
TEST(SizedIntCompileRun, ReturnNarrowLiteral) {
    std::string src = expect_e2e_source("sized_return", R"PURR(import io
fn make() -> list<i16> {
    return [10, 20, 30]
}
io.print(make())
)PURR", "[10, 20, 30]\n");
    EXPECT_NE(src.find("return std::vector<std::int16_t>{10LL, 20LL, 30LL}"), std::string::npos) << src;
}

// A struct of narrow fields PACKS (the whole memory point) and prints its i8/u8 fields as numbers.
TEST(SizedIntCompileRun, StructPacksAndPrintsNumerically) {
    std::string src = expect_e2e_source("sized_struct", R"PURR(import io
struct Cell {
    x: u8
    y: u8
}
fn main() {
    let c: Cell = Cell(3, 7)
    io.print(c)
    io.print(sizeof(Cell))
}
main()
)PURR", "Cell(\n    x = 3,\n    y = 7\n)\n2\n");   // two u8 pack to 2 bytes (vs 16 as long long)
    EXPECT_NE(src.find("std::uint8_t x{};"), std::string::npos) << src;
    EXPECT_NE(src.find("+this->x"), std::string::npos) << src;  // promoted so it prints as a number
}

// The footprint win, proven in-language: element and struct sizes are the narrow widths, and
// `int` itself is untouched (still 8 bytes) so standalone integers never grow or shrink.
TEST(SizedIntCompileRun, SizeofProvesFootprint) {
    expect_e2e("sized_footprint", R"PURR(import io
let a: i8 = 1
io.print(sizeof(a))
io.print(sizeof(i8), sizeof(i16), sizeof(i32), sizeof(i64))
io.print(sizeof(u8), sizeof(u32))
io.print(sizeof(int))
)PURR", "1\n1 2 4 8\n1 4\n8\n");
}

// Arithmetic on narrow operands PROMOTES to 64-bit for free — 100 + 100 as i8 is 200, not the
// wrapped -56 an i8 add would give. Speed and range of the compute are unchanged; only storage
// is narrow. (This is the guarantee that keeps sized ints from silently corrupting results.)
TEST(SizedIntCompileRun, ArithmeticPromotesToWide) {
    expect_e2e("sized_promote", R"PURR(import io
let a: i8 = 100
let b: i8 = 100
io.print(a + b)
)PURR", "200\n");
}

// Storing a wide value into a narrow scalar TRUNCATES at the width (like C / a NumPy fixed dtype):
// 300 into a u8 keeps the low byte, 44. Opt-in, documented, and never a hidden runtime check.
TEST(SizedIntCompileRun, ScalarStoreTruncates) {
    expect_e2e("sized_truncate", R"PURR(import io
let w: u8 = 300
io.print(w)
let big: u64 = 5000000000
io.print(big)
)PURR", "44\n5000000000\n");
}

// A narrow RETURN type binds (`-> i16` => std::int16_t); the body's `n + n` promotes to compute
// then narrows to the return. dbl(50) -> 100. (A narrow SCALAR param stays a generic forwarding
// reference — as every cheatah scalar param does — so the width there documents intent and drives
// the return, while storage widths bind on lets / fields / containers.)
TEST(SizedIntCompileRun, NarrowReturnBinds) {
    std::string src = expect_e2e_source("sized_param", R"PURR(import io
fn dbl(n: i16) -> i16 {
    return n + n
}
io.print(dbl(50))
)PURR", "100\n");
    EXPECT_NE(src.find("std::int16_t dbl("), std::string::npos) << src;  // narrow return type
}

// Mixed-width struct fields each pack to their own width and all print numerically (the i8/u8
// char-print gotcha stays fixed even next to wider fields and a string).
TEST(SizedIntCompileRun, StructMixedWidths) {
    expect_e2e("sized_struct_mixed", R"PURR(import io
struct Rec {
    tag: u8
    id: i32
    name: str
}
fn main() {
    let r: Rec = Rec(9, 100000, "hi")
    io.print(r)
    io.print(sizeof(i32))
}
main()
)PURR", "Rec(\n    tag = 9,\n    id = 100000,\n    name = hi\n)\n4\n");
}

// A literal that does NOT fit its declared width is rejected at COMPILE time (the constant-narrowing
// rule) — a free, zero-runtime-cost bounds check. 300 cannot be stored in a u8 list element.
TEST(SizedIntCompileRun, OverflowLiteralIsCompileError) {
    expect_compile_fail("sized_overflow", R"PURR(import io
let bad: list<u8> = [1, 300, 3]
io.print(bad)
)PURR");
}
