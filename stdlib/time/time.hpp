// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
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
 *
 * Returns fractional seconds since the Unix epoch (1970-01-01 UTC) from the
 * system clock; because it follows real time, it can jump forward or backward
 * when the clock is adjusted (NTP, manual changes) and is not suitable for
 * measuring elapsed intervals.
 * @return seconds since the Unix epoch (`system_clock`).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.WallClockIsRecent
 * @crtest TimeCompileRun.Time
 * @systest StdlibE2E.Time
 */
double time();
/**
 * Wall-clock time.
 *
 * Same wall clock as `time` but returned as an integer count of nanoseconds
 * since the Unix epoch, avoiding the precision loss of `double` seconds; it
 * shares the same caveat that the system clock can be stepped backward.
 * @return nanoseconds since the Unix epoch (`system_clock`).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.WallClockIsRecent
 * @crtest TimeCompileRun.TimeNs
 * @systest StdlibE2E.Time
 */
long long time_ns();

/**
 * Monotonic clock; never runs backwards.
 *
 * Returns fractional seconds from `steady_clock`, which advances steadily and
 * is immune to system-clock adjustments, making it the right choice for timing
 * intervals; its zero point is unspecified, so only differences are meaningful.
 * @return seconds from `steady_clock`.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.MonotonicClocksAdvanceAcrossSleep
 * @crtest TimeCompileRun.Monotonic
 * @systest StdlibE2E.Time
 */
double monotonic();
/**
 * Monotonic clock; never runs backwards.
 *
 * Same monotonic `steady_clock` as `monotonic` but as an integer nanosecond
 * count, preserving full precision; only differences between readings have a
 * defined meaning.
 * @return nanoseconds from `steady_clock`.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.MonotonicClocksAdvanceAcrossSleep
 * @crtest TimeCompileRun.MonotonicNs
 * @systest StdlibE2E.Time
 */
long long monotonic_ns();

/**
 * Highest-resolution monotonic counter.
 *
 * Intended as the finest-grained clock for benchmarking; in this build it is
 * backed by the same `steady_clock` as `monotonic`, so it is monotonic with an
 * unspecified origin and should be used only for elapsed-time measurements.
 * @return seconds from `steady_clock`.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.PerfCounterAdvances
 * @crtest TimeCompileRun.PerfCounter
 * @systest StdlibE2E.Time
 */
double perf_counter();
/**
 * Highest-resolution monotonic counter.
 *
 * Nanosecond-precision form of `perf_counter`, backed by `steady_clock`; like
 * the other monotonic readings, only the difference between two calls is
 * meaningful.
 * @return nanoseconds from `steady_clock`.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.MonotonicClocksAdvanceAcrossSleep
 * @crtest TimeCompileRun.PerfCounterNs
 * @systest StdlibE2E.Time
 */
long long perf_counter_ns();

/**
 * CPU time consumed by this process.
 *
 * Returns processor time used by the program (via `std::clock` / `CLOCKS_PER_SEC`),
 * not wall-clock time, so it excludes time spent sleeping or blocked and may
 * grow faster than real time across multiple threads.
 * @return seconds of CPU time (`std::clock`).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahTime.ProcessTimeNonNegative
 * @crtest TimeCompileRun.ProcessTime
 * @systest StdlibE2E.Time
 */
double process_time();

/**
 * Suspend the calling thread.
 *
 * Blocks the current thread for at least @p seconds (fractional values are
 * honored); the OS may sleep slightly longer due to scheduling, and a
 * non-positive duration returns essentially immediately.
 * @param seconds duration to sleep (fractional).
 * @complexity O(1) plus the sleep wait.
 * @alloc none.
 * @test CheatahTime.MonotonicClocksAdvanceAcrossSleep
 * @crtest TimeCompileRun.Sleep
 * @systest StdlibE2E.Time
 */
void sleep(double seconds);

} // namespace cheatah::time
