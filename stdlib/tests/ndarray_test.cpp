#include "ndarray.hpp"

#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

namespace nd = cheatah::ndarray;

// Security hardening: malicious/buggy shapes and indices must throw, not corrupt
// memory (negative dims -> huge size; product overflow -> under-allocation; OOB
// index -> out-of-bounds read). Matters once untrusted .purr can reach these.
TEST(CheatahNDArray, RejectsMaliciousShapesAndIndices) {
    EXPECT_THROW(nd::zeros({-1}), std::runtime_error);          // negative dimension
    EXPECT_THROW(nd::full({-3, 2}, 1.0), std::runtime_error);   // negative dimension
    const long long big = 1LL << 40;                           // product 2^120 wraps size_t
    EXPECT_THROW(nd::zeros({big, big, big}), std::runtime_error);
    EXPECT_THROW(nd::get(nd::array({1.0, 2.0}), {5}), std::runtime_error);  // OOB index
    EXPECT_THROW(nd::get(nd::array({1.0, 2.0}), {-1}), std::runtime_error); // negative index
    EXPECT_THROW(nd::get(nd::reshape(nd::array({1.0, 2.0, 3.0, 4.0}), {2, 2}), {0}),
                 std::runtime_error);                           // wrong-rank index
}

TEST(CheatahNDArray, ShapeFactoriesAndReductions) {
    const nd::NDArray z = nd::zeros({2, 3});
    EXPECT_EQ(nd::shape_of(z), (std::vector<long long>{2, 3}));
    EXPECT_EQ(nd::size_of(z), 6);
    EXPECT_DOUBLE_EQ(nd::sum(z), 0.0);

    const nd::NDArray o = nd::ones({4});
    EXPECT_DOUBLE_EQ(nd::sum(o), 4.0);
    EXPECT_DOUBLE_EQ(nd::mean(o), 1.0);

    const nd::NDArray a = nd::array({1.0, 2.0, 3.0, 4.0});
    EXPECT_DOUBLE_EQ(nd::sum(a), 10.0);
    EXPECT_DOUBLE_EQ(nd::get(a, {2}), 3.0);
}

TEST(CheatahNDArray, BroadcastShapeRules) {
    // (3,1) + (1,4) -> (3,4)
    EXPECT_EQ(nd::broadcast_shapes({3, 1}, {1, 4}), (std::vector<std::size_t>{3, 4}));
    // (2,3) + (3,) -> (2,3)   (trailing alignment)
    EXPECT_EQ(nd::broadcast_shapes({2, 3}, {3}), (std::vector<std::size_t>{2, 3}));
    // scalar (0-d) broadcasts to anything
    EXPECT_EQ(nd::broadcast_shapes({}, {2, 5}), (std::vector<std::size_t>{2, 5}));
    // incompatible
    EXPECT_THROW(nd::broadcast_shapes({3}, {4}), std::exception);
}

TEST(CheatahNDArray, BroadcastingAdd) {
    // column (3,1) + row (1,3) -> (3,3) outer sum
    nd::NDArray col = nd::reshape(nd::array({0.0, 10.0, 20.0}), {3, 1});
    nd::NDArray row = nd::reshape(nd::array({1.0, 2.0, 3.0}), {1, 3});
    nd::NDArray r = nd::add(col, row);
    EXPECT_EQ(nd::shape_of(r), (std::vector<long long>{3, 3}));
    EXPECT_DOUBLE_EQ(nd::get(r, {0, 0}), 1.0);    // 0 + 1
    EXPECT_DOUBLE_EQ(nd::get(r, {1, 2}), 13.0);   // 10 + 3
    EXPECT_DOUBLE_EQ(nd::get(r, {2, 1}), 22.0);   // 20 + 2
    EXPECT_EQ(nd::to_string(nd::add(nd::array({1.0, 2.0}), nd::scalar(10.0))), "[11, 12]");
}

TEST(CheatahNDArray, ElementwiseAndScalarBroadcast) {
    nd::NDArray a = nd::array({2.0, 4.0, 6.0});
    EXPECT_DOUBLE_EQ(nd::sum(nd::mul(a, nd::scalar(0.5))), 6.0);   // (1+2+3)
    EXPECT_DOUBLE_EQ(nd::get(nd::sub(a, nd::scalar(1.0)), {2}), 5.0);
    EXPECT_DOUBLE_EQ(nd::get(nd::divide(a, nd::scalar(2.0)), {1}), 2.0);
}

TEST(CheatahNDArray, Arange) {
    const nd::NDArray a = nd::arange(0.0, 5.0, 1.0);  // [0,1,2,3,4]
    EXPECT_EQ(nd::size_of(a), 5);
    EXPECT_DOUBLE_EQ(nd::get(a, {0}), 0.0);
    EXPECT_DOUBLE_EQ(nd::get(a, {4}), 4.0);
    const nd::NDArray b = nd::arange(3.0, 0.0, -1.0);  // [3,2,1]
    EXPECT_EQ(nd::size_of(b), 3);
    EXPECT_THROW(nd::arange(0.0, 5.0, 0.0), std::runtime_error);  // zero step
}

TEST(CheatahNDArray, ReshapeSizeMismatchThrows) {
    EXPECT_THROW(nd::reshape(nd::array({1.0, 2.0, 3.0}), {2, 2}), std::runtime_error);
}

TEST(CheatahNDArray, ToStringScalar) {
    EXPECT_EQ(nd::to_string(nd::scalar(42.0)), "42");
}

TEST(CheatahNDArray, ComplexElementType) {
    // A complex (Field) array: stores, accesses, and arithmetic over std::complex.
    using C = std::complex<double>;
    static_assert(nd::is_complex_v<C> && !nd::is_complex_v<double>);
    static_assert(std::is_same_v<nd::real_base_t<C>, double>);
    static_assert(std::is_same_v<nd::complex_of_t<double>, C>);
    const nd::basic_ndarray<C> a = nd::array(std::vector<C>{C(1, 2), C(3, -4), C(0, 1)});
    // Python-style formatting: positive imag -> "a+bj", negative -> "a-bj".
    EXPECT_EQ(nd::to_string(a), "[1+2j, 3-4j, 0+1j]");
    EXPECT_EQ(nd::get(a, {1}), C(3, -4));
    EXPECT_EQ(nd::sum(a), C(4, -1));
    EXPECT_EQ(nd::to_string(nd::add(a, a)), "[2+4j, 6-8j, 0+2j]");
    // A 0-d complex scalar formats without brackets.
    EXPECT_EQ(nd::to_string(nd::scalar(C(5, -6))), "5-6j");
}

TEST(CheatahNDArray, ElementwiseMath) {
    // The array counterparts of the scalar math module (numpy-style ufuncs).
    const nd::NDArray a = nd::array({1.0, 4.0, 9.0, 16.0});
    EXPECT_EQ(nd::to_string(nd::sqrt(a)), "[1, 2, 3, 4]");
    EXPECT_EQ(nd::to_string(nd::cbrt(nd::array({1.0, 8.0, 27.0}))), "[1, 2, 3]");
    EXPECT_DOUBLE_EQ(nd::get(nd::exp(nd::array({0.0, 1.0})), {1}), std::exp(1.0));
    EXPECT_DOUBLE_EQ(nd::get(nd::log(nd::array({1.0, 2.718281828459045})), {1}), std::log(2.718281828459045));
    EXPECT_NEAR(nd::get(nd::sin(nd::array({0.0, 1.5707963267948966})), {1}), 1.0, 1e-12);
    EXPECT_NEAR(nd::get(nd::cos(nd::array({0.0})), {0}), 1.0, 1e-12);
    EXPECT_NEAR(nd::get(nd::tan(nd::array({0.0})), {0}), 0.0, 1e-12);
    EXPECT_EQ(nd::to_string(nd::abs(nd::array({-2.0, 3.0, -4.0}))), "[2, 3, 4]");
    // Shape is preserved (2-D input → 2-D output).
    const nd::NDArray m = nd::reshape(nd::array({1.0, 4.0, 9.0, 16.0}), {2, 2});
    EXPECT_EQ(nd::to_string(nd::sqrt(m)), "[[1, 2], [3, 4]]");
}

TEST(CheatahNDArray, ComplexConstructAndParts) {
    using C = std::complex<double>;
    const nd::NDArray re = nd::array({0.0, 1.0, 2.0});
    const nd::NDArray im = nd::array({1.0, 0.0, -3.0});
    const nd::basic_ndarray<C> z = nd::complex(re, im);   // [0+1j, 1+0j, 2-3j]
    EXPECT_EQ(nd::to_string(z), "[0+1j, 1+0j, 2-3j]");
    EXPECT_EQ(nd::get(z, {2}), C(2, -3));
    // real / imag pull the parts back out as real arrays.
    EXPECT_EQ(nd::to_string(nd::real(z)), "[0, 1, 2]");
    EXPECT_EQ(nd::to_string(nd::imag(z)), "[1, 0, -3]");
    // conj negates the imaginary part; the "1+0j" element keeps a clean +0 (not -0).
    EXPECT_EQ(nd::to_string(nd::conj(z)), "[0-1j, 1+0j, 2+3j]");
    // On a real array: conj is identity, real is a copy, imag is all zeros.
    EXPECT_EQ(nd::to_string(nd::conj(re)), "[0, 1, 2]");
    EXPECT_EQ(nd::to_string(nd::real(re)), "[0, 1, 2]");
    EXPECT_EQ(nd::to_string(nd::imag(re)), "[0, 0, 0]");
    // complex() broadcasts a scalar imaginary part against the real vector.
    EXPECT_EQ(nd::to_string(nd::complex(re, nd::scalar(5.0))), "[0+5j, 1+5j, 2+5j]");
    // A strided (non-contiguous) view exercises map_array's odometer fallback.
    const nd::basic_ndarray<C> zb = nd::broadcast_to(nd::scalar(C(1, 2)), {3});
    EXPECT_EQ(nd::to_string(nd::conj(zb)), "[1-2j, 1-2j, 1-2j]");
}

TEST(CheatahNDArray, BroadcastTo) {
    const nd::NDArray row = nd::array({1.0, 2.0, 3.0});   // shape {3}
    const nd::NDArray b = nd::broadcast_to(row, {2, 3});  // stretch to 2x3
    EXPECT_DOUBLE_EQ(nd::get(b, {0, 2}), 3.0);
    EXPECT_DOUBLE_EQ(nd::get(b, {1, 0}), 1.0);
    const nd::NDArray m = nd::reshape(nd::array({1.0, 2.0, 3.0, 4.0}), {2, 2});
    EXPECT_THROW(nd::broadcast_to(m, {4}), std::runtime_error);    // can't broadcast to fewer dims
    EXPECT_THROW(nd::broadcast_to(row, {2, 4}), std::runtime_error);  // {3} not broadcastable to last dim 4
}

// Cover both element-wise paths: the vectorized contiguous fast path (matching
// shapes, no broadcast) and the C-order odometer fallback (a strided/broadcast
// view), plus the strided-reduction (sum) fallback.
TEST(CheatahNDArray, ContiguousFastPathAndStridedReduce) {
    // Same-shape, contiguous operands -> the std::transform(unseq) fast path.
    const nd::NDArray a = nd::array({1.0, 2.0, 3.0, 4.0});
    const nd::NDArray b = nd::array({10.0, 20.0, 30.0, 40.0});
    const nd::NDArray c = nd::add(a, b);
    EXPECT_DOUBLE_EQ(nd::get(c, {0}), 11.0);
    EXPECT_DOUBLE_EQ(nd::get(c, {3}), 44.0);
    EXPECT_DOUBLE_EQ(nd::get(nd::mul(a, b), {1}), 40.0);
    // Sum of a NON-contiguous (broadcast, stride-0) view -> the odometer fallback.
    const nd::NDArray v = nd::broadcast_to(nd::scalar(2.0), {3});  // [2, 2, 2], stride 0
    EXPECT_DOUBLE_EQ(nd::sum(v), 6.0);
}
