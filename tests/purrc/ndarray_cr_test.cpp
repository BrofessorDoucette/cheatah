// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Compile-run unit tests for the `ndarray` module: one test per function. Each
// writes a tiny .purr that calls a single ndarray function, compiles it with purrc,
// runs it under the cheatah runtime, and asserts the exact stdout. Complements the
// in-process unit tests (stdlib/tests/ndarray_test.cpp) and the per-module
// system-level test (StdlibE2E.Ndarray).
//
// Skipped (not callable from .purr):
//   - broadcast_to / broadcast_shapes: take std::vector<std::size_t>, but cheatah
//     `list<int>` lowers to std::vector<long long>, which doesn't convert.
//   - shape_of returns std::vector<long long>, which io.print can't stream directly;
//     it is exercised here by indexing the returned list (ShapeOf below).
#include "e2e_harness.hpp"

TEST(NdarrayCompileRun, Array) {
    e2e::expect_e2e("ndarray_array", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.array([1.0, 2.0, 3.0])))
)PURR", "[1, 2, 3]\n");
}

TEST(NdarrayCompileRun, Scalar) {
    e2e::expect_e2e("ndarray_scalar", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.scalar(42.0)))
)PURR", "42\n");
}

TEST(NdarrayCompileRun, Zeros) {
    e2e::expect_e2e("ndarray_zeros", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.zeros([2, 3])))
)PURR", "[[0, 0, 0], [0, 0, 0]]\n");
}

TEST(NdarrayCompileRun, Ones) {
    e2e::expect_e2e("ndarray_ones", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.ones([4])))
)PURR", "[1, 1, 1, 1]\n");
}

TEST(NdarrayCompileRun, Full) {
    e2e::expect_e2e("ndarray_full", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.full([2, 2], 7.0)))
)PURR", "[[7, 7], [7, 7]]\n");
}

TEST(NdarrayCompileRun, Arange) {
    e2e::expect_e2e("ndarray_arange", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.arange(0.0, 5.0, 1.0)))
)PURR", "[0, 1, 2, 3, 4]\n");
}

TEST(NdarrayCompileRun, Reshape) {
    e2e::expect_e2e("ndarray_reshape", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.reshape(ndarray.array([1.0, 2.0, 3.0, 4.0]), [2, 2])))
)PURR", "[[1, 2], [3, 4]]\n");
}

TEST(NdarrayCompileRun, Add) {
    e2e::expect_e2e("ndarray_add", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.add(ndarray.array([1.0, 2.0, 3.0]), ndarray.scalar(10.0))))
)PURR", "[11, 12, 13]\n");
}

TEST(NdarrayCompileRun, Sub) {
    e2e::expect_e2e("ndarray_sub", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.sub(ndarray.array([5.0, 7.0, 9.0]), ndarray.scalar(1.0))))
)PURR", "[4, 6, 8]\n");
}

TEST(NdarrayCompileRun, Mul) {
    e2e::expect_e2e("ndarray_mul", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.mul(ndarray.array([2.0, 4.0, 6.0]), ndarray.scalar(0.5))))
)PURR", "[1, 2, 3]\n");
}

TEST(NdarrayCompileRun, Divide) {
    e2e::expect_e2e("ndarray_divide", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.divide(ndarray.array([2.0, 4.0, 6.0]), ndarray.scalar(2.0))))
)PURR", "[1, 2, 3]\n");
}

TEST(NdarrayCompileRun, Sum) {
    e2e::expect_e2e("ndarray_sum", R"PURR(import io
import ndarray
io.print(ndarray.sum(ndarray.array([1.0, 2.0, 3.0, 4.0])))
)PURR", "10\n");
}

TEST(NdarrayCompileRun, Mean) {
    e2e::expect_e2e("ndarray_mean", R"PURR(import io
import ndarray
io.print(ndarray.mean(ndarray.ones([4])))
)PURR", "1\n");
}

TEST(NdarrayCompileRun, Get) {
    e2e::expect_e2e("ndarray_get", R"PURR(import io
import ndarray
io.print(ndarray.get(ndarray.array([1.0, 2.0, 3.0]), [2]))
)PURR", "3\n");
}

TEST(NdarrayCompileRun, ShapeOf) {
    e2e::expect_e2e("ndarray_shape_of", R"PURR(import io
import ndarray
let s = ndarray.shape_of(ndarray.zeros([2, 3]))
io.print(s[0], s[1])
)PURR", "2 3\n");
}

TEST(NdarrayCompileRun, SizeOf) {
    e2e::expect_e2e("ndarray_size_of", R"PURR(import io
import ndarray
io.print(ndarray.size_of(ndarray.zeros([2, 3])))
)PURR", "6\n");
}

TEST(NdarrayCompileRun, ToString) {
    e2e::expect_e2e("ndarray_to_string", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.reshape(ndarray.array([1.0, 2.0, 3.0, 4.0]), [2, 2])))
)PURR", "[[1, 2], [3, 4]]\n");
}

// io.print ABBREVIATES a large array with "..." (readable by default); a small one is full.
TEST(NdarrayCompileRun, PrintAbbreviatesLargeArray) {
    e2e::expect_e2e("ndarray_print_trunc", R"PURR(import io
import ndarray
io.print(ndarray.arange(0.0, 2000.0, 1.0))
)PURR",
                    "[0, 1, 2, ..., 1997, 1998, 1999]\n");
}

// io.rprint shows the array RAW (exactly as stored) — no abbreviation.
TEST(NdarrayCompileRun, RprintShowsArrayFull) {
    e2e::expect_e2e("ndarray_rprint_full", R"PURR(import io
import ndarray
io.rprint(ndarray.arange(0.0, 6.0, 1.0))
)PURR",
                    "[0, 1, 2, 3, 4, 5]\n");
}

TEST(NdarrayCompileRun, Complex) {
    e2e::expect_e2e("ndarray_complex", R"PURR(import io
import ndarray
let z = ndarray.complex(ndarray.array([0.0, 2.0]), ndarray.array([1.0, -3.0]))
io.print(ndarray.to_string(z))
)PURR", "[0+1j, 2-3j]\n");
}

TEST(NdarrayCompileRun, Conj) {
    e2e::expect_e2e("ndarray_conj", R"PURR(import io
import ndarray
let z = ndarray.complex(ndarray.array([0.0, 2.0]), ndarray.array([1.0, -3.0]))
io.print(ndarray.to_string(ndarray.conj(z)))
)PURR", "[0-1j, 2+3j]\n");
}

TEST(NdarrayCompileRun, Real) {
    e2e::expect_e2e("ndarray_real", R"PURR(import io
import ndarray
let z = ndarray.complex(ndarray.array([0.0, 2.0]), ndarray.array([1.0, -3.0]))
io.print(ndarray.to_string(ndarray.real(z)))
)PURR", "[0, 2]\n");
}

TEST(NdarrayCompileRun, Imag) {
    e2e::expect_e2e("ndarray_imag", R"PURR(import io
import ndarray
let z = ndarray.complex(ndarray.array([0.0, 2.0]), ndarray.array([1.0, -3.0]))
io.print(ndarray.to_string(ndarray.imag(z)))
)PURR", "[1, -3]\n");
}

TEST(NdarrayCompileRun, Sqrt) {
    e2e::expect_e2e("ndarray_sqrt", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.sqrt(ndarray.array([1.0, 4.0, 9.0, 16.0]))))
)PURR", "[1, 2, 3, 4]\n");
}

TEST(NdarrayCompileRun, Exp) {
    e2e::expect_e2e("ndarray_exp", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.exp(ndarray.array([0.0]))))
)PURR", "[1]\n");
}

TEST(NdarrayCompileRun, Sin) {
    e2e::expect_e2e("ndarray_sin", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.sin(ndarray.array([0.0]))))
)PURR", "[0]\n");
}

TEST(NdarrayCompileRun, NestedArray) {
    // Nested list literals build N-D arrays directly: 2-D and 3-D from source.
    e2e::expect_e2e("ndarray_nested", R"PURR(import io
import ndarray
let m = ndarray.array([[1.0, 2.0], [3.0, 4.0]])
io.print(ndarray.shape_of(m)[0], ndarray.shape_of(m)[1])
io.print(ndarray.to_string(m))
let t = ndarray.array([[[1.0], [2.0]], [[3.0], [4.0]]])
io.print(ndarray.to_string(t))
)PURR", "2 2\n[[1, 2], [3, 4]]\n[[[1], [2]], [[3], [4]]]\n");
}

TEST(NdarrayCompileRun, Astype) {
    // `arr.astype(<width>)` converts the element type — numpy's a.astype(dtype). The narrow
    // element makes a smaller array (sizeof(i16) == 2), and i8/u8 elements print as NUMBERS.
    e2e::expect_e2e("ndarray_astype", R"PURR(import io
import ndarray
let a = ndarray.array([1, 2, 3]).astype(i16)
io.print(ndarray.to_string(a))
io.print(sizeof(i16))
let b = ndarray.array([65, 200, 9]).astype(u8)
io.print(ndarray.to_string(b))
let f = ndarray.array([1, 2, 3]).astype(f32)
io.print(ndarray.to_string(f))
)PURR", "[1, 2, 3]\n2\n[65, 200, 9]\n[1, 2, 3]\n");
}

TEST(NdarrayCompileRun, AstypeNarrows) {
    // Narrowing truncates/wraps at the target width, like a numpy fixed dtype: 300 -> 44 in u8.
    e2e::expect_e2e("ndarray_astype_narrow", R"PURR(import io
import ndarray
io.print(ndarray.to_string(ndarray.array([300, 256, 255]).astype(u8)))
)PURR", "[44, 0, 255]\n");
}

TEST(NdarrayCompileRun, AstypeConversionsPrintSensibleNumbers) {
    // Narrowing/widening from cheatah source must print SENSIBLE NUMERIC values (never characters),
    // with C / numpy fixed-dtype semantics: signed narrowing wraps two's-complement, unsigned
    // narrowing is modulo 2^bits, signed->unsigned same width reinterprets the bits, float->int
    // truncates toward zero, a widen round-trip recovers the value, and shape is preserved in 2-D.
    e2e::expect_e2e("ndarray_astype_conversions", R"PURR(import io
import ndarray
fn main() {
    io.print(ndarray.to_string(ndarray.array([127, 128, 255, 256, -1, -129]).astype(i8)))
    io.print(ndarray.to_string(ndarray.array([0, 255, 256, 300, -1]).astype(u8)))
    io.print(ndarray.to_string(ndarray.array([-1, -2, 5]).astype(u32)))
    io.print(ndarray.to_string(ndarray.array([3.9, -3.9, 2.99, 255.7]).astype(i32)))
    io.print(ndarray.to_string(ndarray.array([-128, 0, 127]).astype(i8).astype(i64)))
    io.print(ndarray.to_string(ndarray.array([[1, 300], [256, -1]]).astype(u8)))
    io.print(ndarray.to_string(ndarray.array([1, 2, 3]).astype(f64)))
}
main()
)PURR",
        "[127, -128, -1, 0, -1, 127]\n"
        "[0, 255, 0, 44, 255]\n"
        "[4294967295, 4294967294, 5]\n"
        "[3, -3, 2, 255]\n"
        "[-128, 0, 127]\n"
        "[[1, 44], [0, 255]]\n"
        "[1, 2, 3]\n");
}

// Both spellings of a width name reach astype identically, and a declared narrow type prints the
// same sensible numbers as the explicit .astype form.
TEST(NdarrayCompileRun, AstypeSpellingsAndDeclaredAgree) {
    e2e::expect_e2e("ndarray_astype_spellings", R"PURR(import io
import ndarray
fn main() {
    io.print(ndarray.to_string(ndarray.array([1, 2, 300]).astype(int16)))
    io.print(ndarray.to_string(ndarray.array([1, 2, 300]).astype(i16)))
    let a: ndarray<i16> = ndarray.array([1, 2, 300])
    io.print(ndarray.to_string(a))
}
main()
)PURR", "[1, 2, 300]\n[1, 2, 300]\n[1, 2, 300]\n");
}

TEST(NdarrayCompileRun, NarrowElementDeclaredTypeDrives) {
    // A declared `ndarray<i8>` drives construction: the initializer is converted for you, so you
    // do not have to spell `.astype(i8)`. The 2-D shape and numeric i8 printing are preserved.
    e2e::expect_e2e("ndarray_narrow_decl", R"PURR(import io
import ndarray
fn main() {
    let a: ndarray<i8> = ndarray.array([100, 101, 102])
    io.print(ndarray.to_string(a))
    let m: ndarray<u16> = ndarray.array([[1, 2], [3, 4]])
    io.print(ndarray.to_string(m))
}
main()
)PURR", "[100, 101, 102]\n[[1, 2], [3, 4]]\n");
}
