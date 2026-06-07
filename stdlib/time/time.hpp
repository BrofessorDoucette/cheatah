#pragma once

/**
 * @file time.hpp
 * @brief cheatah `time` — high-accuracy timing. Mirrors the timing core of
 *        Python's `time` module, built on C++ `<chrono>` clocks
 *        (`system_clock` for wall-clock, `steady_clock` for monotonic
 *        high-resolution counters). `import time` to use it.
 *
 * Unit tests: `stdlib/tests/time_test.cpp`; the suite runs under
 * AddressSanitizer (the `asan` preset) and Valgrind
 * (`security/run-valgrind.sh`) on every QA-gate run.
 *
 * Doc convention (see also the other stdlib headers): each function documents
 * its runtime complexity with @complexity, its heap allocation with @alloc, and
 * the @test that covers it.
 */
#include <cstdint>

namespace cheatah::time {

/**
 * Wall-clock time.
 * @return seconds since the Unix epoch (`system_clock`).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.WallClockIsRecent
 */
double time();
/**
 * Wall-clock time.
 * @return nanoseconds since the Unix epoch (`system_clock`).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.WallClockIsRecent
 */
long long time_ns();

/**
 * Monotonic clock; never runs backwards.
 * @return seconds from `steady_clock`.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.MonotonicClocksAdvanceAcrossSleep
 */
double monotonic();
/**
 * Monotonic clock; never runs backwards.
 * @return nanoseconds from `steady_clock`.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.MonotonicClocksAdvanceAcrossSleep
 */
long long monotonic_ns();

/**
 * Highest-resolution monotonic counter.
 * @return seconds from `steady_clock`.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.PerfCounterAdvances
 */
double perf_counter();
/**
 * Highest-resolution monotonic counter.
 * @return nanoseconds from `steady_clock`.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.MonotonicClocksAdvanceAcrossSleep
 */
long long perf_counter_ns();

/**
 * CPU time consumed by this process.
 * @return seconds of CPU time (`std::clock`).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.ProcessTimeNonNegative
 */
double process_time();

/**
 * Suspend the calling thread.
 * @param seconds duration to sleep (fractional).
 * @complexity O(1) plus the sleep wait.
 * @alloc none.
 * @test CheatahTime.MonotonicClocksAdvanceAcrossSleep
 */
void sleep(double seconds);

} // namespace cheatah::time
