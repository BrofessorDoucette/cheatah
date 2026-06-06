#include "ndarray.hpp"

#include <vector>

#include <gtest/gtest.h>

namespace nd = cheatah::purrscript::ndarray;

TEST(PurrscriptNDArray, ShapeFactoriesAndReductions) {
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

TEST(PurrscriptNDArray, BroadcastShapeRules) {
    // (3,1) + (1,4) -> (3,4)
    EXPECT_EQ(nd::broadcast_shapes({3, 1}, {1, 4}), (std::vector<std::size_t>{3, 4}));
    // (2,3) + (3,) -> (2,3)   (trailing alignment)
    EXPECT_EQ(nd::broadcast_shapes({2, 3}, {3}), (std::vector<std::size_t>{2, 3}));
    // scalar (0-d) broadcasts to anything
    EXPECT_EQ(nd::broadcast_shapes({}, {2, 5}), (std::vector<std::size_t>{2, 5}));
    // incompatible
    EXPECT_THROW(nd::broadcast_shapes({3}, {4}), std::exception);
}

TEST(PurrscriptNDArray, BroadcastingAdd) {
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

TEST(PurrscriptNDArray, ElementwiseAndScalarBroadcast) {
    nd::NDArray a = nd::array({2.0, 4.0, 6.0});
    EXPECT_DOUBLE_EQ(nd::sum(nd::mul(a, nd::scalar(0.5))), 6.0);   // (1+2+3)
    EXPECT_DOUBLE_EQ(nd::get(nd::sub(a, nd::scalar(1.0)), {2}), 5.0);
    EXPECT_DOUBLE_EQ(nd::get(nd::divide(a, nd::scalar(2.0)), {1}), 2.0);
}
