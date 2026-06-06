#pragma once

// purrscript time — high-accuracy timing. Mirrors the timing core of
// https://docs.python.org/3/library/time.html, built on C++ <chrono> clocks:
//   * system_clock  -> wall-clock (time / time_ns)
//   * steady_clock  -> monotonic, high-resolution counters (monotonic / perf_counter)
// for the accuracy timing-sensitive programs need.
#include <cstdint>

namespace cheatah::purrscript::time {

double time();              // wall-clock seconds since the Unix epoch
long long time_ns();        // wall-clock nanoseconds since the epoch

double monotonic();         // monotonic seconds; never runs backwards
long long monotonic_ns();

double perf_counter();      // highest-resolution monotonic counter, seconds
long long perf_counter_ns();

double process_time();      // CPU time used by this process, seconds

void sleep(double seconds); // suspend for `seconds` (fractional)

} // namespace cheatah::purrscript::time
