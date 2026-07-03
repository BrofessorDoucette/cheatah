// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "statistics.hpp"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

namespace stats = cheatah::statistics;

TEST(CheatahStatistics, SumCountMean) {
    const std::vector<double> d{2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
    EXPECT_DOUBLE_EQ(stats::sum(d), 40.0);
    EXPECT_EQ(stats::count(d), 8u);
    EXPECT_DOUBLE_EQ(stats::mean(d), 5.0);
}

TEST(CheatahStatistics, MedianOddAndEven) {
    EXPECT_DOUBLE_EQ(stats::median(std::vector<double>{3.0, 1.0, 2.0}), 2.0);       // odd → middle
    EXPECT_DOUBLE_EQ(stats::median(std::vector<double>{1.0, 2.0, 3.0, 4.0}), 2.5);  // even → mean of two
}

TEST(CheatahStatistics, PopulationVarianceAndStdev) {
    const std::vector<double> d{2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
    EXPECT_DOUBLE_EQ(stats::pvariance(d), 4.0);  // divide by N
    EXPECT_DOUBLE_EQ(stats::pstdev(d), 2.0);
}

TEST(CheatahStatistics, SampleVarianceAndStdev) {
    const std::vector<double> d{1.0, 2.0, 3.0, 4.0, 5.0};  // mean 3, Σ(x-μ)² = 10
    EXPECT_DOUBLE_EQ(stats::variance(d), 2.5);             // divide by N-1 → 10/4
    EXPECT_DOUBLE_EQ(stats::stdev(d), std::sqrt(2.5));
}
