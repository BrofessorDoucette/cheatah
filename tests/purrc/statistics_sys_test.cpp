// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level (whole-program) test for the `statistics` stdlib module. Unlike
// the per-function compile-run tests (tests/purrc/statistics_cr_test.cpp), this
// drives a single cohesive program over ONE fixed dataset through EVERY
// purr-callable statistics function and asserts its exact stdout, so the
// functions are exercised together against the same data.
//
// Fixed dataset {2,4,4,4,5,5,7,9}: sum 40, count 8, mean 5, pvariance 4,
// pstdev 2, variance 32/7 (≈4.57143), stdev sqrt(32/7) (≈2.13809), median 4.5
// (mean of the two middle values 4 and 5). Fully deterministic.
//
// Coverage — every function in stdlib/statistics/statistics.hpp:
//   sum, count, mean, pvariance, pstdev, variance, stdev, median.
#include "e2e_harness.hpp"

TEST(StdlibE2E, Statistics) {
    e2e::expect_e2e("statistics_sys", R"PURR(import io
import statistics

let xs = [2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0]

io.print(statistics.sum(xs))
io.print(statistics.count(xs))
io.print(statistics.mean(xs))
io.print(statistics.pvariance(xs))
io.print(statistics.pstdev(xs))
io.print(statistics.variance(xs))
io.print(statistics.stdev(xs))
io.print(statistics.median(xs))
)PURR",
                        "40\n"
                        "8\n"
                        "5\n"
                        "4\n"
                        "2\n"
                        "4.57143\n"
                        "2.13809\n"
                        "4.5\n");
}
