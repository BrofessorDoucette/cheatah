#include "time.hpp"

#include <gtest/gtest.h>

namespace t = cheatah::purrscript::time;

TEST(PurrscriptTime, MonotonicClocksAdvanceAcrossSleep) {
    const double m0 = t::monotonic();
    const long long p0 = t::perf_counter_ns();
    t::sleep(0.01);  // 10 ms
    EXPECT_GE(t::monotonic() - m0, 0.005);  // at least ~5 ms elapsed
    EXPECT_GT(t::perf_counter_ns() - p0, 0);
    EXPECT_GT(t::monotonic_ns(), 0);
}

TEST(PurrscriptTime, WallClockIsRecent) {
    EXPECT_GT(t::time(), 1.6e9);  // after ~2020-09
    EXPECT_GT(t::time_ns(), 0);
}

TEST(PurrscriptTime, ProcessTimeNonNegative) {
    EXPECT_GE(t::process_time(), 0.0);
}
