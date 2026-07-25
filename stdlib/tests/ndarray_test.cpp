// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "ndarray.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <type_traits>
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

TEST(CheatahNDArray, CompoundAssignInPlace) {
    // += / -= / *= / /= mutate the SAME buffer (no reallocation) on the
    // contiguous fast path — the property hot loops rely on to reuse one array.
    nd::basic_ndarray<double> a = nd::array(std::vector<double>{1.0, 2.0, 3.0});
    const void* buf = a.buffer().get();
    a += nd::array(std::vector<double>{10.0, 20.0, 30.0});
    a *= 2.0;
    a -= 1.0;
    a /= 3.0;
    EXPECT_EQ(a.buffer().get(), buf) << "compound assignment must not reallocate";
    EXPECT_DOUBLE_EQ(nd::get(a, {0}), 7.0);    // ((1+10)*2 - 1) / 3
    EXPECT_DOUBLE_EQ(nd::get(a, {2}), 21.6666666666666667);
    // Infix scalar multiply allocates a NEW array and leaves the source alone.
    const nd::basic_ndarray<double> doubled = a * 2.0;
    EXPECT_DOUBLE_EQ(nd::get(doubled, {0}), 14.0);
    EXPECT_DOUBLE_EQ(nd::get(a, {0}), 7.0);
}

TEST(CheatahNDArray, BinaryOpIntoReusesBuffer) {
    // add(out, a, b): the user-provided-output form writes into out's OWN buffer — no reallocation.
    nd::basic_ndarray<double> a = nd::array(std::vector<double>{1.0, 2.0, 3.0});
    nd::basic_ndarray<double> b = nd::array(std::vector<double>{10.0, 20.0, 30.0});
    nd::basic_ndarray<double> out = nd::zeros({3});
    const void* obuf = out.buffer().get();
    nd::add(out, a, b);
    EXPECT_EQ(out.buffer().get(), obuf) << "out-param add must not reallocate";
    EXPECT_DOUBLE_EQ(nd::get(out, {0}), 11.0);
    EXPECT_DOUBLE_EQ(nd::get(out, {2}), 33.0);
    nd::mul(out, out, a);  // may alias a full-shape operand (index-local write)
    EXPECT_DOUBLE_EQ(nd::get(out, {2}), 99.0);  // 33 * 3
}

TEST(CheatahNDArray, BinaryOpIntoBroadcastPaths) {
    // The out-form must handle every broadcast layout binary_op_into distinguishes:
    //   (1) array ⊕ scalar   — a is full-shape/contiguous, b is a 0-d scalar
    //   (2) scalar ⊕ array   — a is a 0-d scalar, b is full-shape/contiguous
    //   (3) strided fallback — neither operand is full-shape contiguous after broadcast
    nd::basic_ndarray<double> a = nd::array(std::vector<double>{1.0, 2.0, 3.0, 4.0});
    const nd::basic_ndarray<double> s = nd::scalar(10.0);

    // (1) array ⊕ scalar into out.
    nd::basic_ndarray<double> o1 = nd::zeros({4});
    nd::add(o1, a, s);
    EXPECT_DOUBLE_EQ(nd::get(o1, {0}), 11.0);
    EXPECT_DOUBLE_EQ(nd::get(o1, {3}), 14.0);

    // (2) scalar ⊕ array into out — subtraction proves the operand order (s - b, not b - s).
    nd::basic_ndarray<double> o2 = nd::zeros({4});
    nd::sub(o2, s, a);
    EXPECT_DOUBLE_EQ(nd::get(o2, {0}), 9.0);   // 10 - 1
    EXPECT_DOUBLE_EQ(nd::get(o2, {3}), 6.0);   // 10 - 4

    // (3) strided fallback: a (3,1) column and a (1,3) row both broadcast to (3,3), so NEITHER
    // operand is full-shape contiguous (each carries a stride-0 axis) — the do/next_index loop.
    const nd::basic_ndarray<double> col = nd::reshape(nd::array({0.0, 10.0, 20.0}), {3, 1});
    const nd::basic_ndarray<double> row = nd::reshape(nd::array({1.0, 2.0, 3.0}), {1, 3});
    nd::basic_ndarray<double> o3 = nd::zeros({3, 3});
    nd::add(o3, col, row);
    EXPECT_DOUBLE_EQ(nd::get(o3, {0, 0}), 1.0);    // 0 + 1
    EXPECT_DOUBLE_EQ(nd::get(o3, {2, 2}), 23.0);   // 20 + 3
    EXPECT_DOUBLE_EQ(nd::get(o3, {1, 0}), 11.0);   // 10 + 1
}

TEST(CheatahNDArray, RvalueOperandReusesBuffer) {
    // "copy vs move": a temporary LEFT operand is computed into IN PLACE and moved out, so the result
    // adopts that buffer — no new allocation. `a + b + c` thus allocates once (for `a + b`), not twice.
    nd::basic_ndarray<double> a = nd::array(std::vector<double>{1.0, 2.0, 3.0});
    nd::basic_ndarray<double> b = nd::array(std::vector<double>{10.0, 20.0, 30.0});
    const void* abuf = a.buffer().get();
    nd::basic_ndarray<double> r = std::move(a) + b;            // reuses the expiring a's buffer
    EXPECT_EQ(r.buffer().get(), abuf) << "rvalue + must reuse the left operand's buffer";
    EXPECT_DOUBLE_EQ(nd::get(r, {1}), 22.0);
    // an lvalue `x + y` still allocates (it can't clobber a named array) — value semantics preserved.
    nd::basic_ndarray<double> x = nd::array(std::vector<double>{1.0, 1.0});
    nd::basic_ndarray<double> y = nd::array(std::vector<double>{2.0, 2.0});
    const nd::basic_ndarray<double> sum = x + y;
    EXPECT_NE(sum.buffer().get(), x.buffer().get());
    EXPECT_DOUBLE_EQ(nd::get(x, {0}), 1.0) << "lvalue operand must be untouched";
}

TEST(CheatahNDArray, RvalueOperandSymmetric) {
    // `a + std::move(b)` must reuse a buffer exactly like `std::move(a) + b` — neither allocates, and
    // the right-operand form reuses the RIGHT buffer. For non-commutative ops the value still matches.
    {  // right operand reused; same value as the allocating form
        nd::basic_ndarray<double> a = nd::array(std::vector<double>{1.0, 2.0, 3.0});
        nd::basic_ndarray<double> b = nd::array(std::vector<double>{10.0, 20.0, 30.0});
        const void* bbuf = b.buffer().get();
        nd::basic_ndarray<double> r = a + std::move(b);
        EXPECT_EQ(r.buffer().get(), bbuf) << "a + rvalue must reuse the right operand's buffer";
        EXPECT_DOUBLE_EQ(nd::get(r, {2}), 33.0);
    }
    {  // subtraction is non-commutative: a - move(b) must still be a-b, not b-a
        nd::basic_ndarray<double> a = nd::array(std::vector<double>{10.0, 20.0});
        nd::basic_ndarray<double> b = nd::array(std::vector<double>{1.0, 2.0});
        const void* bbuf = b.buffer().get();
        nd::basic_ndarray<double> r = a - std::move(b);
        EXPECT_EQ(r.buffer().get(), bbuf) << "a - rvalue must reuse the right operand's buffer";
        EXPECT_DOUBLE_EQ(nd::get(r, {0}), 9.0);   // 10 - 1, NOT 1 - 10
        EXPECT_DOUBLE_EQ(nd::get(r, {1}), 18.0);
    }
    {  // division reversed combiner: a / move(b) == a/b
        nd::basic_ndarray<double> a = nd::array(std::vector<double>{6.0, 8.0});
        nd::basic_ndarray<double> b = nd::array(std::vector<double>{2.0, 4.0});
        nd::basic_ndarray<double> r = a / std::move(b);
        EXPECT_DOUBLE_EQ(nd::get(r, {0}), 3.0);   // 6 / 2
        EXPECT_DOUBLE_EQ(nd::get(r, {1}), 2.0);   // 8 / 4
    }
    {  // both operands expiring: the LEFT buffer wins (chain-accumulator semantics)
        nd::basic_ndarray<double> a = nd::array(std::vector<double>{1.0, 1.0});
        nd::basic_ndarray<double> b = nd::array(std::vector<double>{2.0, 2.0});
        const void* abuf = a.buffer().get();
        nd::basic_ndarray<double> r = std::move(a) + std::move(b);
        EXPECT_EQ(r.buffer().get(), abuf) << "both-rvalue must reuse the LEFT operand's buffer";
        EXPECT_DOUBLE_EQ(nd::get(r, {0}), 3.0);
    }
}

TEST(CheatahNDArray, ScalarTimesSizeOneArrayKeepsShape) {
    // Regression: `scalar OP size-1 array` must broadcast to the ARRAY's shape, NOT collapse to the
    // scalar's 0-d shape. The 0-d `scalar(s)` temporary must never be reused as the result buffer.
    nd::NDArray v = nd::array({3.0});            // shape {1}
    nd::NDArray r = 0.5 * v;                      // scalar on the LEFT
    EXPECT_EQ(r.ndim(), 1u);
    EXPECT_EQ(r.size(), 1u);
    EXPECT_DOUBLE_EQ(nd::get(r, {0}), 1.5);
    nd::NDArray r2 = v * 2.0;                     // scalar on the RIGHT
    EXPECT_EQ(r2.ndim(), 1u);
    EXPECT_DOUBLE_EQ(nd::get(r2, {0}), 6.0);
    nd::NDArray r3 = 1.0 - v;                     // non-commutative, scalar left: 1 - 3 = -2
    EXPECT_DOUBLE_EQ(nd::get(r3, {0}), -2.0);
    // a multi-element array still broadcasts correctly (the path that always worked)
    nd::NDArray w = nd::array({1.0, 2.0, 3.0});
    nd::NDArray rw = 10.0 * w;
    EXPECT_EQ(rw.size(), 3u);
    EXPECT_DOUBLE_EQ(nd::get(rw, {2}), 30.0);
}

TEST(CheatahNDArray, LikeFactories) {
    nd::NDArray a = nd::reshape(nd::array({1.0, 2.0, 3.0, 4.0}), {2, 2});
    const nd::NDArray z = nd::zeros_like(a);
    EXPECT_EQ(nd::shape_of(z), (std::vector<long long>{2, 2}));
    EXPECT_DOUBLE_EQ(nd::get(z, {1, 1}), 0.0);
    EXPECT_DOUBLE_EQ(nd::get(nd::ones_like(a), {0, 0}), 1.0);
    EXPECT_DOUBLE_EQ(nd::get(nd::full_like(a, 7.0), {1, 0}), 7.0);
    EXPECT_DOUBLE_EQ(nd::get(a, {0, 0}), 1.0) << "source must be untouched";
}

TEST(CheatahNDArray, SubscriptReadWrite) {
    // item_ref/operator[]: negative-aware element writes; builtins::index reads.
    nd::basic_ndarray<long long> m = nd::array(std::vector<long long>{0, 0, 0});
    m[0] = 1;
    m.item_ref(-1) = 7;
    EXPECT_EQ(cheatah::builtins::index(m, 0), 1);
    EXPECT_EQ(cheatah::builtins::index(m, -1), 7);
    nd::basic_ndarray<double> w =
        nd::reshape(nd::array(std::vector<double>{1, 2, 3, 4}), {2, 2});
    w.item_ref(1, 0) = 9.0;
    EXPECT_DOUBLE_EQ(cheatah::builtins::index(w, 1, 0), 9.0);
    EXPECT_THROW(m.item_ref(5), std::out_of_range);
    EXPECT_THROW(w.item_ref(0), std::out_of_range);  // wrong rank
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

TEST(CheatahNDArray, StreamableOperator) {
    // An NDArray is directly Streamable (operator<<) — the FULL form, like str()/to_string.
    std::ostringstream os;
    os << nd::array({1.0, 2.0, 3.0});
    EXPECT_EQ(os.str(), "[1, 2, 3]");
}

TEST(CheatahNDArray, PrettyPrintAbbreviatesLarge) {
    // io.print's hook: a small array prints in full; a large one (past the threshold)
    // abbreviates each long axis with "..." (numpy-style edge items).
    std::ostringstream small;
    nd::arange(0.0, 6.0, 1.0).cheatah_pretty_print(small, 0);
    EXPECT_EQ(small.str(), "[0, 1, 2, 3, 4, 5]");
    EXPECT_EQ(small.str().find("..."), std::string::npos);

    std::ostringstream big;
    nd::arange(0.0, 1500.0, 1.0).cheatah_pretty_print(big, 0);
    EXPECT_NE(big.str().find("..."), std::string::npos);            // abbreviated
    EXPECT_EQ(big.str().rfind("[0, 1, 2, ...,", 0), 0u);            // first edge items kept
}

TEST(CheatahNDArray, RprintFormIsFullNeverAbbreviated) {
    // The rprint/str/to_string path shows the WHOLE array, even when large (no "...").
    const std::string full = nd::to_string(nd::arange(0.0, 1500.0, 1.0));
    EXPECT_EQ(full.find("..."), std::string::npos);
    EXPECT_NE(full.find("750"), std::string::npos);  // a middle element io.print would omit
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

// ---- coverage: ufunc scalar-walk fallback + binary-op scalar/broadcast paths ----
TEST(CheatahNDArray, UfuncStridedFallback) {
    // A broadcast (non-contiguous) array forces the scalar map fallback in each ufunc
    // (the contiguous-double path runs the precompiled SIMD kernel instead).
    EXPECT_NEAR(nd::get(nd::sqrt(nd::broadcast_to(nd::scalar(4.0), {3})), {0}), 2.0, 1e-12);
    EXPECT_NEAR(nd::get(nd::cbrt(nd::broadcast_to(nd::scalar(8.0), {2})), {0}), 2.0, 1e-12);
    EXPECT_NEAR(nd::get(nd::exp(nd::broadcast_to(nd::scalar(0.0), {2})), {0}), 1.0, 1e-12);
    EXPECT_NEAR(nd::get(nd::log(nd::broadcast_to(nd::scalar(1.0), {2})), {0}), 0.0, 1e-12);
    EXPECT_NEAR(nd::get(nd::sin(nd::broadcast_to(nd::scalar(0.0), {2})), {0}), 0.0, 1e-12);
    EXPECT_NEAR(nd::get(nd::cos(nd::broadcast_to(nd::scalar(0.0), {2})), {0}), 1.0, 1e-12);
    EXPECT_NEAR(nd::get(nd::tan(nd::broadcast_to(nd::scalar(0.0), {2})), {0}), 0.0, 1e-12);
}

TEST(CheatahNDArray, BinaryOpScalarAndBroadcast) {
    const nd::NDArray v = nd::array({1.0, 2.0, 3.0});
    const nd::NDArray s = nd::scalar(10.0);
    // array ⊕ scalar (fast path) and scalar ⊕ array (reverse fast path) for each op
    EXPECT_EQ(nd::to_string(nd::add(v, s)), "[11, 12, 13]");
    EXPECT_EQ(nd::to_string(nd::add(s, v)), "[11, 12, 13]");
    EXPECT_EQ(nd::to_string(nd::sub(v, s)), "[-9, -8, -7]");
    EXPECT_EQ(nd::to_string(nd::sub(s, v)), "[9, 8, 7]");
    EXPECT_EQ(nd::to_string(nd::mul(v, s)), "[10, 20, 30]");
    EXPECT_EQ(nd::to_string(nd::mul(s, v)), "[10, 20, 30]");
    EXPECT_NEAR(nd::get(nd::divide(v, nd::scalar(2.0)), {1}), 1.0, 1e-12);
    EXPECT_NEAR(nd::get(nd::divide(nd::scalar(6.0), v), {2}), 2.0, 1e-12);
    // a genuine (non-scalar) broadcast: 2x3 ⊕ length-3 row -> the strided C-order walk
    const nd::NDArray m = nd::reshape(nd::array({0.0, 0.0, 0.0, 10.0, 10.0, 10.0}), {2, 3});
    EXPECT_EQ(nd::to_string(nd::add(m, v)), "[[1, 2, 3], [11, 12, 13]]");
    EXPECT_EQ(nd::to_string(nd::sub(m, v)), "[[-1, -2, -3], [9, 8, 7]]");
    EXPECT_EQ(nd::to_string(nd::mul(m, v)), "[[0, 0, 0], [10, 20, 30]]");
    EXPECT_NEAR(nd::get(nd::divide(m, v), {1, 1}), 5.0, 1e-12);
}

// ==========================================================================
//  N-dimensional construction (1-D vector → 5-D), the whole point of an NDArray.
//  array([...]) reads the shape off the nesting and flattens C-order; shape/get/
//  reductions and numpy-style broadcasting then work at every rank.
// ==========================================================================
TEST(CheatahNDArray, Dim1Vector) {
    // 1-D — a plain vector.
    const nd::NDArray v = nd::array(std::vector<double>{1.0, 2.0, 3.0});
    EXPECT_EQ(nd::shape_of(v), (std::vector<long long>{3}));
    EXPECT_EQ(nd::to_string(v), "[1, 2, 3]");
    EXPECT_DOUBLE_EQ(nd::get(v, {2}), 3.0);
}

TEST(CheatahNDArray, Dim2Matrix) {
    // 2-D — a matrix (a vector of equal-length rows).
    const nd::NDArray m =
        nd::array(std::vector<std::vector<double>>{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}});
    EXPECT_EQ(nd::shape_of(m), (std::vector<long long>{2, 3}));
    EXPECT_EQ(nd::to_string(m), "[[1, 2, 3], [4, 5, 6]]");
    EXPECT_DOUBLE_EQ(nd::get(m, {1, 2}), 6.0);
}

TEST(CheatahNDArray, Dim3VectorOfMatrices) {
    // 3-D — a vector of 2×2 matrices (shape 2×2×2).
    using M = std::vector<std::vector<double>>;
    const nd::NDArray t = nd::array(std::vector<M>{
        {{1.0, 2.0}, {3.0, 4.0}}, {{5.0, 6.0}, {7.0, 8.0}}});
    EXPECT_EQ(nd::shape_of(t), (std::vector<long long>{2, 2, 2}));
    EXPECT_DOUBLE_EQ(nd::get(t, {1, 0, 1}), 6.0);
    EXPECT_DOUBLE_EQ(nd::sum(t), 36.0);
    // numpy-style broadcasting at 3-D: a [2,1] column stretches over each plane's rows.
    const nd::NDArray col = nd::array(std::vector<std::vector<double>>{{10.0}, {20.0}});
    EXPECT_EQ(nd::to_string(nd::add(t, col)),
              "[[[11, 12], [23, 24]], [[15, 16], [27, 28]]]");
}

TEST(CheatahNDArray, Dim4VectorOfVectorsOfMatrices) {
    // 4-D — a vector of vectors of 2×2 matrices (shape 2×1×2×2).
    using M = std::vector<std::vector<double>>;   // 2-D
    using T3 = std::vector<M>;                     // 3-D
    const nd::NDArray a = nd::array(std::vector<T3>{
        {{{1.0, 2.0}, {3.0, 4.0}}},
        {{{5.0, 6.0}, {7.0, 8.0}}}});
    EXPECT_EQ(nd::shape_of(a), (std::vector<long long>{2, 1, 2, 2}));
    EXPECT_DOUBLE_EQ(nd::get(a, {1, 0, 1, 1}), 8.0);
    EXPECT_DOUBLE_EQ(nd::sum(a), 36.0);
    // a 0-d scalar broadcasts across the whole 4-D array.
    EXPECT_DOUBLE_EQ(nd::get(nd::mul(a, nd::scalar(2.0)), {0, 0, 1, 0}), 6.0);
}

TEST(CheatahNDArray, Dim5VectorOfVectorsOfVectorsOfMatrices) {
    // 5-D — a vector of vectors of vectors of 2×2 matrices (shape 2×1×1×2×2).
    using M = std::vector<std::vector<double>>;
    using T3 = std::vector<M>;
    using T4 = std::vector<T3>;
    const nd::NDArray a = nd::array(std::vector<T4>{
        {{{{1.0, 2.0}, {3.0, 4.0}}}},
        {{{{5.0, 6.0}, {7.0, 8.0}}}}});
    EXPECT_EQ(nd::shape_of(a), (std::vector<long long>{2, 1, 1, 2, 2}));
    EXPECT_DOUBLE_EQ(nd::get(a, {1, 0, 0, 0, 1}), 6.0);
    EXPECT_DOUBLE_EQ(nd::sum(a), 36.0);
    // 5-D broadcasting: a trailing [2,2] matrix adds into every plane.
    const nd::NDArray bias =
        nd::array(std::vector<std::vector<double>>{{100.0, 200.0}, {300.0, 400.0}});
    EXPECT_DOUBLE_EQ(nd::get(nd::add(a, bias), {0, 0, 0, 1, 1}), 404.0);
}

TEST(CheatahNDArray, NestedArrayConstruction) {
    // Shape inferred from the nesting; the leaf scalar type is deduced (integer here).
    const auto m =
        nd::array(std::vector<std::vector<long long>>{{1, 2, 3}, {4, 5, 6}});
    EXPECT_EQ(nd::shape_of(m), (std::vector<long long>{2, 3}));
    EXPECT_EQ(nd::to_string(m), "[[1, 2, 3], [4, 5, 6]]");
    // Ragged nested lists are rejected, exactly as numpy rejects them — at the top
    // level (rows differ)…
    EXPECT_THROW(nd::array(std::vector<std::vector<double>>{{1.0, 2.0}, {3.0}}),
                 std::runtime_error);
    // …and deeper (inner planes differ).
    using M = std::vector<std::vector<double>>;
    EXPECT_THROW(nd::array(std::vector<M>{{{1.0, 2.0}}, {{3.0}}}), std::runtime_error);
}

TEST(CheatahNDArray, NestedArrayRaggedAtEveryDepth) {
    // The rectangularity check is in the `nested_collect` template, so each rank gets
    // its OWN throw — trigger ragged at 3-D, 4-D, 5-D so every instantiation is covered.
    using M = std::vector<std::vector<double>>;   // 2-D
    using T3 = std::vector<M>;                     // 3-D
    using T4 = std::vector<T3>;                    // 4-D
    // Mismatch the NUMBER OF SUB-BLOCKS at each rank (not just leaf-row lengths) so each
    // nested_collect<U=…> instantiation's rectangularity throw is exercised.
    EXPECT_THROW(nd::array(std::vector<M>{M{{1.0}}, M{{1.0}, {2.0}}}), std::runtime_error);
    EXPECT_THROW(nd::array(std::vector<T3>{T3{M{{1.0}}}, T3{M{{1.0}}, M{{2.0}}}}),
                 std::runtime_error);
    EXPECT_THROW(
        nd::array(std::vector<T4>{T4{T3{M{{1.0}}}}, T4{T3{M{{1.0}}}, T3{M{{2.0}}}}}),
        std::runtime_error);
}

TEST(CheatahNDArray, ReshapeStridedSource) {
    // Reshaping a NON-contiguous (broadcast, stride-0) source takes the odometer
    // fallback, not the contiguous memcpy fast path.
    const nd::NDArray b = nd::broadcast_to(nd::scalar(2.0), {6});  // stride-0 view, size 6
    const nd::NDArray r = nd::reshape(b, {2, 3});
    EXPECT_EQ(nd::shape_of(r), (std::vector<long long>{2, 3}));
    EXPECT_DOUBLE_EQ(nd::get(r, {1, 2}), 2.0);
    EXPECT_DOUBLE_EQ(nd::sum(r), 12.0);
}

// ---- Coverage of the remaining error branches and general N-D loops --------
// Assertions are structural (throws / shape / sum / element) rather than relying
// on formatted output, so they stay robust.

// array(...) rejects a ragged nested list (a row whose length differs from siblings).
TEST(CheatahNDArray, RaggedNestedListThrows) {
    EXPECT_THROW(nd::array(std::vector<std::vector<double>>{{1.0, 2.0, 3.0}, {4.0, 5.0}}),
                 std::runtime_error);
}

// item_ref: wrong rank and out-of-range (including negative wraparound past the start).
TEST(CheatahNDArray, ItemRefRankAndRangeErrors) {
    nd::basic_ndarray<double> v = nd::array(std::vector<double>{1.0, 2.0, 3.0});
    EXPECT_THROW(v.item_ref(0, 0), std::out_of_range);   // too many indices for a 1-D array
    EXPECT_THROW(v.item_ref(3), std::out_of_range);      // past the end
    EXPECT_THROW(v.item_ref(-4), std::out_of_range);     // negative wraps before the start
    nd::basic_ndarray<double> m = nd::reshape(nd::array(std::vector<double>{1, 2, 3, 4}), {2, 2});
    EXPECT_THROW(m.item_ref(0), std::out_of_range);      // too few indices for a 2-D array
    EXPECT_THROW(m.item_ref(0, 5), std::out_of_range);   // column out of range
}

// at(index-vector): wrong number of dimensions and a coordinate out of range.
TEST(CheatahNDArray, AtVectorRankAndRangeErrors) {
    const nd::NDArray m = nd::reshape(nd::array({1.0, 2.0, 3.0, 4.0}), {2, 2});
    EXPECT_THROW(nd::get(m, {0}), std::runtime_error);     // wrong number of dims
    EXPECT_THROW(nd::get(m, {0, 5}), std::runtime_error);  // coordinate out of range
    EXPECT_THROW(nd::get(m, {2, 0}), std::runtime_error);
}

// A binary op between shapes that don't broadcast must throw, not corrupt memory.
TEST(CheatahNDArray, BinaryOpNonBroadcastableThrows) {
    const nd::NDArray a = nd::array({1.0, 2.0, 3.0});        // {3}
    const nd::NDArray b = nd::array({1.0, 2.0, 3.0, 4.0});   // {4}
    EXPECT_THROW(nd::add(a, b), std::exception);
    EXPECT_THROW(nd::mul(a, b), std::exception);
}

// reshape to an incompatible total size throws (extra shapes beyond the existing case).
TEST(CheatahNDArray, ReshapeWrongTotalSizeThrows) {
    const nd::NDArray a = nd::array({1.0, 2.0, 3.0, 4.0, 5.0, 6.0});  // size 6
    EXPECT_THROW(nd::reshape(a, {4, 2}), std::runtime_error);  // wants 8
    EXPECT_THROW(nd::reshape(a, {5}), std::runtime_error);     // wants 5
}

// reshape of a non-contiguous MULTI-dim view drives the general odometer flatten loop.
TEST(CheatahNDArray, ReshapeStridedMultiDimSource) {
    const nd::NDArray row = nd::array({1.0, 2.0, 3.0});
    const nd::NDArray bc = nd::broadcast_to(row, {2, 3});  // non-contiguous 2-D view -> [[1,2,3],[1,2,3]]
    ASSERT_EQ(nd::shape_of(bc), (std::vector<long long>{2, 3}));
    const nd::NDArray r = nd::reshape(bc, {3, 2});         // flattens [1,2,3,1,2,3] then reshapes
    EXPECT_EQ(nd::shape_of(r), (std::vector<long long>{3, 2}));
    EXPECT_DOUBLE_EQ(nd::sum(r), 12.0);
    EXPECT_DOUBLE_EQ(nd::get(r, {0, 0}), 1.0);
    EXPECT_DOUBLE_EQ(nd::get(r, {1, 0}), 3.0);
    EXPECT_DOUBLE_EQ(nd::get(r, {2, 1}), 3.0);
}

// Scalar-broadcast fast paths: one operand is a single element (b.size()==1 and a.size()==1).
TEST(CheatahNDArray, BinaryOpScalarFastPaths) {
    const nd::NDArray a = nd::array({1.0, 2.0, 3.0, 4.0});  // contiguous, size 4
    const nd::NDArray one = nd::array({10.0});              // single element
    const nd::NDArray sumr = nd::add(a, one);              // b.size()==1 path
    EXPECT_DOUBLE_EQ(nd::sum(sumr), 50.0);                 // 11+12+13+14
    EXPECT_DOUBLE_EQ(nd::get(sumr, {3}), 14.0);
    EXPECT_DOUBLE_EQ(nd::sum(nd::sub(one, a)), 30.0);     // a.size()==1 path: 9+8+7+6
    EXPECT_DOUBLE_EQ(nd::sum(nd::mul(one, a)), 100.0);    // 10+20+30+40
}

// Equal-shape and broadcasting elementwise ops on 3-D arrays drive the general N-D loop.
TEST(CheatahNDArray, BinaryOpThreeDimEqualAndBroadcast) {
    using M = std::vector<std::vector<double>>;
    const nd::NDArray t = nd::array(std::vector<M>{{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}});  // {2,2,2}
    const nd::NDArray u = nd::array(std::vector<M>{{{10, 20}, {30, 40}}, {{50, 60}, {70, 80}}});
    const nd::NDArray s = nd::add(t, u);                  // equal-shape general path
    EXPECT_EQ(nd::shape_of(s), (std::vector<long long>{2, 2, 2}));
    EXPECT_DOUBLE_EQ(nd::sum(s), 396.0);                  // 36 + 360
    EXPECT_DOUBLE_EQ(nd::get(s, {0, 0, 0}), 11.0);
    EXPECT_DOUBLE_EQ(nd::get(s, {1, 1, 1}), 88.0);
    // Broadcasting a {2,1} column across the {2,2,2} block.
    const nd::NDArray col = nd::array(std::vector<std::vector<double>>{{100.0}, {200.0}});  // {2,1}
    const nd::NDArray bsum = nd::add(t, col);
    EXPECT_EQ(nd::shape_of(bsum), (std::vector<long long>{2, 2, 2}));
    EXPECT_DOUBLE_EQ(nd::get(bsum, {0, 0, 0}), 101.0);
    EXPECT_DOUBLE_EQ(nd::get(bsum, {0, 1, 0}), 203.0);
    EXPECT_DOUBLE_EQ(nd::get(bsum, {1, 1, 1}), 208.0);
}

// sum() over a LARGE contiguous array (the 8-wide unrolled SIMD path), over a
// NON-contiguous view (the general odometer fallback), and to_string of a 0-D scalar.
TEST(CheatahNDArray, SumPathsAndScalarFormat) {
    std::vector<double> big(16);
    for (int i = 0; i < 16; ++i) big[static_cast<std::size_t>(i)] = i + 1;  // 1..16 -> 136
    EXPECT_DOUBLE_EQ(nd::sum(nd::array(big)), 136.0);             // unrolled SIMD block(s)
    const nd::NDArray bc = nd::broadcast_to(nd::array({1.0, 2.0, 3.0}), {4, 3});  // 4*(1+2+3)=24
    EXPECT_DOUBLE_EQ(nd::sum(bc), 24.0);                          // non-contiguous -> odometer sum
    EXPECT_FALSE(nd::to_string(nd::scalar(7.5)).empty());        // 0-D -> format_scalar
}

// In-place compound assignment whose RHS is a NON-contiguous (broadcast) view of size > 1
// takes the `a = binary_op(a, b, op)` fallback rather than either contiguous fast path —
// exercised for every compound operator (+= -= *= /=), each a separate instantiation.
TEST(CheatahNDArray, CompoundAssignNonContiguousFallback) {
    const nd::NDArray b = nd::broadcast_to(nd::array({10.0, 20.0, 30.0}), {2, 3});  // stride-0 view
    {
        nd::basic_ndarray<double> a = nd::reshape(nd::array({1.0, 2.0, 3.0, 4.0, 5.0, 6.0}), {2, 3});
        a += b;
        EXPECT_DOUBLE_EQ(nd::get(a, {0, 0}), 11.0);
        EXPECT_DOUBLE_EQ(nd::get(a, {1, 2}), 36.0);
    }
    {
        nd::basic_ndarray<double> a = nd::full({2, 3}, 100.0);
        a -= b;
        EXPECT_DOUBLE_EQ(nd::get(a, {0, 0}), 90.0);   // 100 - 10
        EXPECT_DOUBLE_EQ(nd::get(a, {1, 2}), 70.0);   // 100 - 30
    }
    {
        nd::basic_ndarray<double> a = nd::full({2, 3}, 2.0);
        a *= b;
        EXPECT_DOUBLE_EQ(nd::get(a, {0, 1}), 40.0);   // 2 * 20
    }
    {
        nd::basic_ndarray<double> a = nd::full({2, 3}, 60.0);
        a /= b;
        EXPECT_DOUBLE_EQ(nd::get(a, {0, 2}), 2.0);    // 60 / 30
    }
}

// The same error/edge paths on an INTEGER (long long) element type — array() ragged check,
// subscript/at rank+range, reshape mismatch, non-broadcastable, the general N-D loops and the
// in-place fallback — so the long-long instantiation of each is covered too (not just double).
TEST(CheatahNDArray, ErrorAndLoopPathsLongLong) {
    using V = std::vector<long long>;
    EXPECT_THROW(nd::array(std::vector<V>{{1, 2, 3}, {4, 5}}), std::runtime_error);  // ragged
    nd::basic_ndarray<long long> v = nd::array(V{1, 2, 3});
    EXPECT_THROW(v.item_ref(0, 0), std::out_of_range);   // wrong rank
    EXPECT_THROW(v.item_ref(5), std::out_of_range);      // out of range
    const nd::basic_ndarray<long long> m = nd::reshape(nd::array(V{1, 2, 3, 4}), {2, 2});
    EXPECT_THROW(nd::get(m, {0}), std::runtime_error);     // wrong dims
    EXPECT_THROW(nd::get(m, {5, 5}), std::runtime_error);  // out of range
    EXPECT_THROW(nd::reshape(nd::array(V{1, 2, 3}), {2, 2}), std::runtime_error);  // size mismatch
    EXPECT_THROW(nd::add(nd::array(V{1, 2, 3}), nd::array(V{1, 2, 3, 4})), std::exception);  // no broadcast
    // general odometer reshape + general N-D elementwise + in-place non-contiguous fallback.
    const nd::basic_ndarray<long long> bc = nd::broadcast_to(nd::array(V{1, 2, 3}), {2, 3});
    EXPECT_EQ(nd::sum(nd::reshape(bc, {3, 2})), 12);
    nd::basic_ndarray<long long> a = nd::reshape(nd::array(V{1, 2, 3, 4, 5, 6}), {2, 3});
    a += bc;
    EXPECT_EQ(nd::get(a, {0, 0}), 2);
    EXPECT_EQ(nd::get(a, {1, 2}), 9);
}

// An ndarray stores fixed-size STRUCTS too, not just numbers — a 2-D point / GPU vertex / colour.
// Elements are MOVED into the buffer (no copy); the numeric surface stays Field-only; and a MOVE-ONLY
// element still stores/indexes/moves but cannot be deep-copied (the copy path does not even compile).
namespace {
struct P2 { double x; double y; };
std::ostream& operator<<(std::ostream& os, const P2& p) { return os << "(" << p.x << "," << p.y << ")"; }
struct MoveOnly { std::unique_ptr<int> p; };
}  // namespace

// The concept split that makes storage-vs-numeric-vs-copy work.
static_assert(nd::Element<double> && nd::Copyable<double>, "numbers store + copy");
static_assert(nd::Element<P2> && nd::Copyable<P2>, "POD struct stores + copies");
static_assert(nd::Element<MoveOnly> && !nd::Copyable<MoveOnly>, "move-only stores but cannot deep-copy");

TEST(CheatahNDArray, ArrayMoveIn) {
    // POD struct: MOVE-IN construction (a temporary binds the rvalue overload), index, size, print.
    nd::basic_ndarray<P2> pts = nd::array(std::vector<P2>{{0.0, 1.0}, {2.0, 3.0}, {4.0, 5.0}});
    EXPECT_EQ(nd::size_of(pts), 3);
    EXPECT_EQ(pts[1].x, 2.0);
    EXPECT_EQ(pts[2].y, 5.0);
    EXPECT_EQ(nd::to_string(pts), "[(0,1), (2,3), (4,5)]");
    EXPECT_EQ(nd::get(pts, {1}).y, 3.0);                     // get() by value (Copyable struct)
    EXPECT_EQ(nd::shape_of(pts).size(), 1u);
    EXPECT_THROW(nd::get(pts, {5}), std::runtime_error);     // OOB index -> at() error path
    EXPECT_THROW(nd::get(pts, {0, 0}), std::runtime_error);  // wrong rank -> at() error path
    EXPECT_THROW(pts.item_ref(9), std::out_of_range);        // subscript OOB
    EXPECT_THROW(pts.item_ref(0, 0), std::out_of_range);     // subscript wrong rank

    // The copying overload (named lvalue) also works for a copyable struct.
    std::vector<P2> src{{7.0, 8.0}};
    nd::basic_ndarray<P2> one = nd::array(src);
    EXPECT_EQ(one[0].x, 7.0);

    // Copying an ndarray CONTAINER is a cheap shared-buffer view (no element copy) — mutation aliases.
    nd::basic_ndarray<P2> view = pts;
    view[0].x = 99.0;
    EXPECT_EQ(pts[0].x, 99.0);

    // MOVE-ONLY element: move-in only, indexed by reference; no deep copy exists.
    std::vector<MoveOnly> mv;
    mv.push_back(MoveOnly{std::make_unique<int>(7)});
    mv.push_back(MoveOnly{std::make_unique<int>(9)});
    nd::basic_ndarray<MoveOnly> ma = nd::array(std::move(mv));
    EXPECT_EQ(nd::size_of(ma), 2);
    EXPECT_EQ(*ma[0].p, 7);
    EXPECT_EQ(*ma[1].p, 9);
}

// astype<U> converts the element type: widen (long long -> double), narrow (long long -> uint8_t,
// which truncates at the width, like a numpy fixed dtype), and preserve shape. The result's
// value_type is exactly U — this is how a narrow-element (small-footprint) ndarray is built.
TEST(CheatahNDArray, AstypeNarrowsAndWidens) {
    const auto src = nd::array<long long>({1, 2, 300});

    auto wide = nd::astype<double>(src);
    static_assert(std::is_same_v<decltype(wide)::value_type, double>);
    EXPECT_DOUBLE_EQ(nd::get(wide, {0}), 1.0);
    EXPECT_DOUBLE_EQ(nd::get(wide, {2}), 300.0);

    auto narrow = nd::astype<std::uint8_t>(src);
    static_assert(std::is_same_v<decltype(narrow)::value_type, std::uint8_t>);
    EXPECT_EQ(nd::get(narrow, {0}), std::uint8_t{1});
    EXPECT_EQ(nd::get(narrow, {2}), std::uint8_t{44});  // 300 wraps to 44 in a byte
    EXPECT_EQ(nd::shape_of(narrow), nd::shape_of(src));  // shape preserved

    // Shape is preserved through a 2-D narrowing conversion too.
    auto m = nd::reshape(nd::array<long long>({1, 2, 3, 4}), {2, 2});
    auto mi = nd::astype<std::int16_t>(m);
    static_assert(std::is_same_v<decltype(mi)::value_type, std::int16_t>);
    EXPECT_EQ(nd::shape_of(mi), (std::vector<long long>{2, 2}));
    EXPECT_EQ(nd::get(mi, {1, 1}), std::int16_t{4});
}

// WIDENING never changes a value (the destination holds it exactly): across int widths, and from
// integer to floating point. Values that fit stay identical.
TEST(CheatahNDArray, AstypeWideningPreservesValues) {
    const auto s = nd::array<long long>({-128, 0, 42, 127});
    const auto i8 = nd::astype<std::int8_t>(s);          // all fit i8
    const auto up16 = nd::astype<std::int16_t>(i8);      // i8  -> i16
    const auto up64 = nd::astype<std::int64_t>(i8);      // i8  -> i64
    const auto upf = nd::astype<double>(i8);             // i8  -> double
    for (std::size_t k = 0; k < 4; ++k) {
        EXPECT_EQ(nd::get(up16, {(long long)k}), std::int16_t(nd::get(i8, {(long long)k})));
        EXPECT_EQ(nd::get(up64, {(long long)k}), std::int64_t(nd::get(i8, {(long long)k})));
        EXPECT_DOUBLE_EQ(nd::get(upf, {(long long)k}), double(nd::get(i8, {(long long)k})));
    }
    EXPECT_EQ(nd::get(up64, {0}), -128);
    EXPECT_EQ(nd::get(up64, {3}), 127);
    // unsigned widening: u8 -> u32 keeps the magnitude.
    const auto u8 = nd::astype<std::uint8_t>(nd::array<long long>({0, 200, 255}));
    const auto u32 = nd::astype<std::uint32_t>(u8);
    EXPECT_EQ(nd::get(u32, {1}), std::uint32_t{200});
    EXPECT_EQ(nd::get(u32, {2}), std::uint32_t{255});
}

// NARROWING SIGNED: values outside [min,max] wrap modulo 2^bits into two's-complement range,
// exactly as a C cast / numpy fixed dtype. Values that fit are unchanged (including negatives).
TEST(CheatahNDArray, AstypeNarrowingSignedWraps) {
    const auto s = nd::array<long long>({127, 128, 255, 256, -128, -129, -1, -100});
    const auto i8 = nd::astype<std::int8_t>(s);
    EXPECT_EQ(nd::get(i8, {0}), std::int8_t{127});    // fits
    EXPECT_EQ(nd::get(i8, {1}), std::int8_t{-128});   // 128  -> -128
    EXPECT_EQ(nd::get(i8, {2}), std::int8_t{-1});     // 255  -> -1
    EXPECT_EQ(nd::get(i8, {3}), std::int8_t{0});      // 256  -> 0
    EXPECT_EQ(nd::get(i8, {4}), std::int8_t{-128});   // fits
    EXPECT_EQ(nd::get(i8, {5}), std::int8_t{127});    // -129 -> 127
    EXPECT_EQ(nd::get(i8, {6}), std::int8_t{-1});     // fits
    EXPECT_EQ(nd::get(i8, {7}), std::int8_t{-100});   // fits
}

// NARROWING UNSIGNED: modulo 2^bits, so negatives become their two's-complement bit pattern.
TEST(CheatahNDArray, AstypeNarrowingUnsignedWraps) {
    const auto s = nd::array<long long>({0, 255, 256, 300, -1, -256, 511});
    const auto u8 = nd::astype<std::uint8_t>(s);
    EXPECT_EQ(nd::get(u8, {0}), std::uint8_t{0});
    EXPECT_EQ(nd::get(u8, {1}), std::uint8_t{255});
    EXPECT_EQ(nd::get(u8, {2}), std::uint8_t{0});     // 256 -> 0
    EXPECT_EQ(nd::get(u8, {3}), std::uint8_t{44});    // 300 -> 44
    EXPECT_EQ(nd::get(u8, {4}), std::uint8_t{255});   // -1  -> 255
    EXPECT_EQ(nd::get(u8, {5}), std::uint8_t{0});     // -256 -> 0
    EXPECT_EQ(nd::get(u8, {6}), std::uint8_t{255});   // 511 -> 255
}

// FLOAT -> INT truncates toward zero (drops the fraction), same-width sign reinterpretation, and
// INT -> FLOAT is exact for these small magnitudes. Round-trips that stay in range recover the int.
TEST(CheatahNDArray, AstypeFloatIntAndSignReinterpret) {
    const auto f = nd::array<double>({3.9, -3.9, 2.99, -0.5, 255.7});
    const auto i = nd::astype<std::int32_t>(f);
    EXPECT_EQ(nd::get(i, {0}), 3);
    EXPECT_EQ(nd::get(i, {1}), -3);
    EXPECT_EQ(nd::get(i, {2}), 2);
    EXPECT_EQ(nd::get(i, {3}), 0);
    EXPECT_EQ(nd::get(i, {4}), 255);
    // int -> float -> int round trip is exact when the value fits.
    const auto back = nd::astype<std::int32_t>(nd::astype<double>(nd::array<long long>({7, -3, 100})));
    EXPECT_EQ(nd::get(back, {0}), 7);
    EXPECT_EQ(nd::get(back, {1}), -3);
    EXPECT_EQ(nd::get(back, {2}), 100);
    // signed -> unsigned SAME width: bit-reinterpretation (-1 -> UINT32_MAX).
    const auto u = nd::astype<std::uint32_t>(nd::array<long long>({-1, -2, 5}));
    EXPECT_EQ(nd::get(u, {0}), std::uint32_t{4294967295u});
    EXPECT_EQ(nd::get(u, {1}), std::uint32_t{4294967294u});
    EXPECT_EQ(nd::get(u, {2}), std::uint32_t{5});
}

// The converted array RENDERS its elements as NUMBERS (never characters), with correct signs, for
// the byte-width types — the property that makes a narrow array actually readable.
TEST(CheatahNDArray, AstypeCharWidthPrintsNumeric) {
    EXPECT_EQ(nd::to_string(nd::astype<std::uint8_t>(nd::array<long long>({65, 66, 250}))),
              "[65, 66, 250]");
    EXPECT_EQ(nd::to_string(nd::astype<std::int8_t>(nd::array<long long>({-1, 0, 65}))),
              "[-1, 0, 65]");
    EXPECT_EQ(nd::to_string(nd::astype<std::int16_t>(nd::array<long long>({-1000, 1000}))),
              "[-1000, 1000]");
}

// NON-CONTIGUOUS source: astype must take the C-order odometer walk (not the contiguous fast
// path) and still convert every element. A broadcast view (stride 0) is the non-contiguous case.
TEST(CheatahNDArray, AstypeNonContiguousSource) {
    const auto row = nd::array<long long>({10, 20, 300});   // shape {3}
    const auto b = nd::broadcast_to(row, {2, 3});           // stretch to 2x3 — stride 0, non-contiguous
    const auto u8 = nd::astype<std::uint8_t>(b);
    static_assert(std::is_same_v<decltype(u8)::value_type, std::uint8_t>);
    EXPECT_EQ(nd::shape_of(u8), (std::vector<long long>{2, 3}));
    for (long long r = 0; r < 2; ++r) {
        EXPECT_EQ(nd::get(u8, {r, 0}), std::uint8_t{10});
        EXPECT_EQ(nd::get(u8, {r, 1}), std::uint8_t{20});
        EXPECT_EQ(nd::get(u8, {r, 2}), std::uint8_t{44});   // 300 wraps to 44 in a byte
    }
}

TEST(CheatahNDArray, DivideInfixLvalueForm) {
    // The lvalue `a / b` infix (the operator form of divide()): a fresh broadcast quotient,
    // with both named operands left untouched.
    const nd::basic_ndarray<double> a = nd::array(std::vector<double>{6.0, 9.0, 12.0});
    const nd::basic_ndarray<double> b = nd::array(std::vector<double>{3.0});  // broadcasts
    const nd::basic_ndarray<double> q = a / b;
    EXPECT_DOUBLE_EQ(nd::get(q, {0}), 2.0);
    EXPECT_DOUBLE_EQ(nd::get(q, {1}), 3.0);
    EXPECT_DOUBLE_EQ(nd::get(q, {2}), 4.0);
    EXPECT_NE(q.buffer().get(), a.buffer().get()) << "lvalue / must allocate a fresh result";
    EXPECT_DOUBLE_EQ(nd::get(a, {0}), 6.0) << "operands must be untouched";
    EXPECT_DOUBLE_EQ(nd::get(b, {0}), 3.0);
    // Elementwise (equal shapes) as well as broadcast.
    const nd::basic_ndarray<double> c = nd::array(std::vector<double>{2.0, 3.0, 4.0});
    EXPECT_DOUBLE_EQ(nd::get(a / c, {2}), 3.0);
}
