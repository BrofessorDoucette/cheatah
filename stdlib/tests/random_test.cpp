#include "random.hpp"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

namespace rnd = cheatah::random;

TEST(CheatahRandom, RandomInUnitInterval) {
    rnd::seed(42);
    for (int i = 0; i < 100; ++i) {
        const double r = rnd::random();
        EXPECT_GE(r, 0.0);
        EXPECT_LT(r, 1.0);
    }
}

TEST(CheatahRandom, UniformInRange) {
    rnd::seed(1);
    for (int i = 0; i < 100; ++i) {
        const double u = rnd::uniform(-2.0, 3.0);
        EXPECT_GE(u, -2.0);
        EXPECT_LE(u, 3.0);
    }
}

TEST(CheatahRandom, RandintInclusiveRange) {
    rnd::seed(2);
    for (int i = 0; i < 100; ++i) {
        const long long n = rnd::randint(1, 6);
        EXPECT_GE(n, 1);
        EXPECT_LE(n, 6);
    }
    EXPECT_EQ(rnd::randint(5, 5), 5);  // degenerate single-value range
}

TEST(CheatahRandom, GaussIsFiniteAndReproducible) {
    rnd::seed(3);
    const double g = rnd::gauss(0.0, 1.0);
    EXPECT_TRUE(std::isfinite(g));
    rnd::seed(3);
    EXPECT_DOUBLE_EQ(rnd::gauss(0.0, 1.0), g);
}

TEST(CheatahRandom, SeedMakesTheStreamReproducible) {
    rnd::seed(7);
    const double a = rnd::random();
    const long long b = rnd::randint(1, 1000000);
    const double c = rnd::uniform(-5.0, 5.0);
    rnd::seed(7);
    EXPECT_DOUBLE_EQ(rnd::random(), a);
    EXPECT_EQ(rnd::randint(1, 1000000), b);
    EXPECT_DOUBLE_EQ(rnd::uniform(-5.0, 5.0), c);
}

TEST(CheatahRandom, Choice) {
    EXPECT_EQ(rnd::choice(std::vector<int>{99, 99, 99}), 99);  // all-equal → always 99
    rnd::seed(4);
    const std::vector<int> xs{1, 2, 3, 4, 5};
    const int picked = rnd::choice(xs);
    EXPECT_GE(picked, 1);
    EXPECT_LE(picked, 5);
}
