// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Compile-run unit tests for the `statistics` module: one test per function.
// Each writes a tiny .purr that calls a single statistics function over a fixed
// list, compiles it with purrc, runs it under the cheatah runtime, and asserts
// the exact stdout. Complements the in-process unit tests
// (stdlib/tests/statistics_test.cpp) and the per-module system-level test
// (StdlibE2E.Statistics).
#include "e2e_harness.hpp"

// Fixed dataset {2,4,4,4,5,5,7,9}: sum 40, count 8, mean 5, pvariance 4,
// pstdev 2. Sample stats use {1,2,3,4,5}: variance 2.5, stdev sqrt(2.5),
// median 3.

TEST(StatisticsCompileRun, Sum) {
    e2e::expect_e2e("statistics_sum", R"PURR(import io
import statistics
let xs = [2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0]
io.print(statistics.sum(xs))
)PURR", "40\n");
}

TEST(StatisticsCompileRun, Count) {
    e2e::expect_e2e("statistics_count", R"PURR(import io
import statistics
let xs = [2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0]
io.print(statistics.count(xs))
)PURR", "8\n");
}

TEST(StatisticsCompileRun, Mean) {
    e2e::expect_e2e("statistics_mean", R"PURR(import io
import statistics
let xs = [2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0]
io.print(statistics.mean(xs))
)PURR", "5\n");
}

TEST(StatisticsCompileRun, Pvariance) {
    e2e::expect_e2e("statistics_pvariance", R"PURR(import io
import statistics
let xs = [2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0]
io.print(statistics.pvariance(xs))
)PURR", "4\n");
}

TEST(StatisticsCompileRun, Pstdev) {
    e2e::expect_e2e("statistics_pstdev", R"PURR(import io
import statistics
let xs = [2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0]
io.print(statistics.pstdev(xs))
)PURR", "2\n");
}

TEST(StatisticsCompileRun, Variance) {
    e2e::expect_e2e("statistics_variance", R"PURR(import io
import statistics
let xs = [1.0, 2.0, 3.0, 4.0, 5.0]
io.print(statistics.variance(xs))
)PURR", "2.5\n");
}

TEST(StatisticsCompileRun, Stdev) {
    e2e::expect_e2e("statistics_stdev", R"PURR(import io
import statistics
let xs = [1.0, 2.0, 3.0, 4.0, 5.0]
io.print(statistics.stdev(xs))
)PURR", "1.58114\n");
}

TEST(StatisticsCompileRun, Median) {
    e2e::expect_e2e("statistics_median", R"PURR(import io
import statistics
let xs = [1.0, 2.0, 3.0, 4.0, 5.0]
io.print(statistics.median(xs))
)PURR", "3\n");
}
