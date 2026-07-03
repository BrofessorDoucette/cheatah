// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level test for the cheatah `time` module: a single cohesive .purr
// program that exercises EVERY public function in stdlib/time/time.hpp
// (time, time_ns, monotonic, monotonic_ns, perf_counter, perf_counter_ns,
// process_time, sleep), compiles it with purrc into a loadable module, runs it
// under the cheatah runtime, and asserts the exact stdout byte-for-byte.
//
// Clock readings are non-deterministic, so the program prints DETERMINISTIC
// boolean PROPERTIES instead of raw values:
//   - wall clock (time/time_ns) is strictly positive;
//   - the monotonic / perf-counter clocks are positive and, after a sleep,
//     read >= their earlier values (they never run backward);
//   - process_time (CPU time) is non-negative.
// Complements the in-process unit tests (stdlib/tests/time_test.cpp) and the
// per-function compile-run tests (tests/purrc/time_cr_test.cpp).
#include "e2e_harness.hpp"

TEST(StdlibE2E, Time) {
    e2e::expect_e2e("time_sys", R"PURR(import io
import time

# Wall-clock readings are positive.
let w = time.time()
let wn = time.time_ns()

# Monotonic / perf clocks are positive and never run backward across a sleep.
let m0 = time.monotonic()
let mn0 = time.monotonic_ns()
let p0 = time.perf_counter()
let pn0 = time.perf_counter_ns()

time.sleep(0.01)

let m1 = time.monotonic()
let mn1 = time.monotonic_ns()
let p1 = time.perf_counter()
let pn1 = time.perf_counter_ns()

# CPU time consumed by the process is non-negative.
let cpu = time.process_time()

io.print(w > 0.0, wn > 0)
io.print(m0 > 0.0, mn0 > 0, p0 > 0.0, pn0 > 0)
io.print(m1 >= m0, mn1 >= mn0, p1 >= p0, pn1 >= pn0)
io.print(cpu >= 0.0)
)PURR",
                    "True True\n"
                    "True True True True\n"
                    "True True True True\n"
                    "True\n");
}
