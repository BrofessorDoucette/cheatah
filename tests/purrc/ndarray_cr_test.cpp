// Compile-run unit tests for the `ndarray` module: one test per function. Each
// writes a tiny .purr that calls a single ndarray function, compiles it with purrc,
// runs it under the cheatah runtime, and asserts the exact stdout. Complements the
// in-process unit tests (stdlib/tests/ndarray_test.cpp) and the per-module
// system-level test (StdlibE2E.Ndarray).
//
// Skipped (not callable from .purr):
//   - broadcast_to / broadcast_shapes: take std::vector<std::size_t>, but cheatah
//     `list[int]` lowers to std::vector<long long>, which doesn't convert.
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
