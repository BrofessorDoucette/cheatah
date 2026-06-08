// System-level (whole-program) test for the `ndarray` stdlib module. Unlike the
// per-function compile-run tests (tests/purrc/ndarray_cr_test.cpp), this drives a
// single cohesive numeric program through EVERY purr-callable ndarray function and
// asserts its exact stdout, so the functions are exercised together (factories feed
// reshape, reshape feeds elementwise ops, those feed reductions / indexing).
//
// Coverage — every purr-callable function in stdlib/ndarray/ndarray.hpp:
//   array, scalar, zeros, ones, full, arange, reshape, add, sub, mul, divide,
//   sum, mean, get, shape_of, size_of, to_string.
//
// Skipped (not callable from .purr, same as the cr test):
//   - broadcast_to / broadcast_shapes: take std::vector<std::size_t>, but cheatah
//     `list[int]` lowers to std::vector<long long>, which doesn't convert.
//   - the NDArray class methods (shape/strides/ndim/size/at/buffer/offset/ctors)
//     are C++-side internals reached only through the free functions above.
#include "e2e_harness.hpp"

TEST(StdlibE2E, Ndarray) {
    e2e::expect_e2e("ndarray_sys", R"PURR(import io
import ndarray

# --- factories ---
let a = ndarray.array([1.0, 2.0, 3.0, 4.0, 5.0, 6.0])
let s = ndarray.scalar(2.0)
let z = ndarray.zeros([2, 3])
let o = ndarray.ones([2, 3])
let f = ndarray.full([2, 3], 4.0)
let r = ndarray.arange(0.0, 6.0, 1.0)

# --- reshape into a 2x3 matrix ---
let m = ndarray.reshape(a, [2, 3])
io.print(ndarray.to_string(m))

# --- elementwise ops (broadcasting against scalar / same shape) ---
let summ = ndarray.add(m, o)
let diff = ndarray.sub(m, s)
let prod = ndarray.mul(m, s)
let quot = ndarray.divide(m, s)
io.print(ndarray.to_string(summ))
io.print(ndarray.to_string(diff))
io.print(ndarray.to_string(prod))
io.print(ndarray.to_string(quot))

# combine zeros / full / arange
let combo = ndarray.add(ndarray.add(z, f), ndarray.reshape(r, [2, 3]))
io.print(ndarray.to_string(combo))

# --- reductions ---
io.print(ndarray.sum(m))
io.print(ndarray.mean(m))

# --- access / shape introspection ---
io.print(ndarray.get(m, [1, 2]))
let sh = ndarray.shape_of(m)
io.print(sh[0], sh[1])
io.print(ndarray.size_of(m))
)PURR",
                    "[[1, 2, 3], [4, 5, 6]]\n"
                    "[[2, 3, 4], [5, 6, 7]]\n"
                    "[[-1, 0, 1], [2, 3, 4]]\n"
                    "[[2, 4, 6], [8, 10, 12]]\n"
                    "[[0.5, 1, 1.5], [2, 2.5, 3]]\n"
                    "[[4, 5, 6], [7, 8, 9]]\n"
                    "21\n"
                    "3.5\n"
                    "6\n"
                    "2 3\n"
                    "6\n");
}

// Complex support (complex/real/imag/conj) exercised together end-to-end: build a
// complex vector from real & imaginary parts, pull the parts back out, conjugate it.
TEST(StdlibE2E, NdarrayComplex) {
    e2e::expect_e2e("ndarray_complex_sys", R"PURR(import io
import ndarray

let re = ndarray.array([0.0, 1.0, 2.0])
let im = ndarray.array([1.0, 0.0, -3.0])
let z = ndarray.complex(re, im)

io.print(ndarray.to_string(z))
io.print(ndarray.to_string(ndarray.conj(z)))
io.print(ndarray.to_string(ndarray.real(z)))
io.print(ndarray.to_string(ndarray.imag(z)))
)PURR",
                    "[0+1j, 1+0j, 2-3j]\n"
                    "[0-1j, 1+0j, 2+3j]\n"
                    "[0, 1, 2]\n"
                    "[1, 0, -3]\n");
}
