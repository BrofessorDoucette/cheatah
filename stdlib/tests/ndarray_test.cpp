#include "ndarray.hpp"

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

TEST(CheatahNDArray, BroadcastTo) {
    const nd::NDArray row = nd::array({1.0, 2.0, 3.0});   // shape {3}
    const nd::NDArray b = nd::broadcast_to(row, {2, 3});  // stretch to 2x3
    EXPECT_DOUBLE_EQ(nd::get(b, {0, 2}), 3.0);
    EXPECT_DOUBLE_EQ(nd::get(b, {1, 0}), 1.0);
    const nd::NDArray m = nd::reshape(nd::array({1.0, 2.0, 3.0, 4.0}), {2, 2});
    EXPECT_THROW(nd::broadcast_to(m, {4}), std::runtime_error);    // can't broadcast to fewer dims
    EXPECT_THROW(nd::broadcast_to(row, {2, 4}), std::runtime_error);  // {3} not broadcastable to last dim 4
}
