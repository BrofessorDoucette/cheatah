#include "math.hpp"

#include <gtest/gtest.h>

namespace m = cheatah::math;

TEST(CheatahMath, Constants) {
    EXPECT_NEAR(m::pi, 3.14159265, 1e-7);
    EXPECT_NEAR(m::e, 2.71828182, 1e-7);
    EXPECT_NEAR(m::tau, 2.0 * m::pi, 1e-12);
    EXPECT_TRUE(m::isinf(m::inf));
    EXPECT_TRUE(m::isnan(m::nan));
}

TEST(CheatahMath, BuiltinLikeOps) {
    EXPECT_EQ(m::abs(-7), 7);
    EXPECT_EQ(m::min(3, 9, 1, 5), 1);
    EXPECT_EQ(m::max(3, 9, 1, 5), 9);
    EXPECT_DOUBLE_EQ(m::pow(2, 10), 1024.0);
}

TEST(CheatahMath, ScalarFunctions) {
    EXPECT_DOUBLE_EQ(m::sqrt(144.0), 12.0);
    EXPECT_DOUBLE_EQ(m::floor(2.7), 2.0);
    EXPECT_DOUBLE_EQ(m::ceil(2.1), 3.0);
    EXPECT_DOUBLE_EQ(m::round(2.5), 3.0);
    EXPECT_NEAR(m::log2(8.0), 3.0, 1e-12);
    EXPECT_NEAR(m::hypot(3.0, 4.0), 5.0, 1e-12);
    EXPECT_NEAR(m::degrees(m::pi), 180.0, 1e-9);
}

TEST(CheatahMath, Integer) {
    EXPECT_EQ(m::gcd(54, 24), 6);
    EXPECT_EQ(m::factorial(5), 120);
}
