#include "datetime.hpp"
#include "hashlib.hpp"
#include "random.hpp"
#include "statistics.hpp"

#include <vector>

#include <gtest/gtest.h>

namespace dt = cheatah::datetime;
namespace rnd = cheatah::random;
namespace stats = cheatah::statistics;
namespace hl = cheatah::hashlib;

TEST(CheatahDatetime, ComponentsAndFormat) {
    // 2021-01-01 00:00:00 UTC = 1609459200. Use a fixed epoch via format/components.
    const double e = 1609459200.0;
    // Components are local-time, so just assert sane ranges + format round-trip shape.
    EXPECT_GE(dt::year(e), 2020);
    EXPECT_GE(dt::month(e), 1);
    EXPECT_LE(dt::month(e), 12);
    EXPECT_EQ(dt::format(e, "%Y").size(), 4u);
    EXPECT_GT(dt::timestamp(), 1.6e9);
    EXPECT_EQ(dt::now().size(), 19u);  // "YYYY-MM-DD HH:MM:SS"
}

TEST(CheatahRandom, Ranges) {
    rnd::seed(42);
    for (int i = 0; i < 50; ++i) {
        const double r = rnd::random();
        EXPECT_GE(r, 0.0);
        EXPECT_LT(r, 1.0);
        const long long n = rnd::randint(1, 6);
        EXPECT_GE(n, 1);
        EXPECT_LE(n, 6);
    }
    // Reproducibility with a fixed seed.
    rnd::seed(7);
    const double a = rnd::random();
    rnd::seed(7);
    const double b = rnd::random();
    EXPECT_DOUBLE_EQ(a, b);
    EXPECT_EQ(rnd::choice(std::vector<int>{99, 99, 99}), 99);
}

TEST(CheatahStatistics, DescriptiveStats) {
    const std::vector<double> d{2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
    EXPECT_DOUBLE_EQ(stats::mean(d), 5.0);
    EXPECT_DOUBLE_EQ(stats::pvariance(d), 4.0);  // population variance = 4
    EXPECT_DOUBLE_EQ(stats::pstdev(d), 2.0);
    EXPECT_DOUBLE_EQ(stats::median(d), 4.5);
    EXPECT_DOUBLE_EQ(stats::sum(d), 40.0);
    EXPECT_EQ(stats::count(d), 8u);
}

TEST(CheatahHashlib, Sha256KnownVectors) {
    EXPECT_EQ(hl::sha256(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(hl::sha256("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
