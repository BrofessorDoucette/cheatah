// Compile-run unit tests for the `time` module: one test per function. Each writes
// a tiny .purr that calls a single time function, compiles it with purrc, runs it
// under the cheatah runtime, and asserts the exact stdout. Complements the
// in-process unit tests (stdlib/tests/time_test.cpp) and the per-module
// system-level test (StdlibE2E.Time).
//
// Clock values are non-deterministic, so every program prints a DETERMINISTIC
// boolean property (ordering / sign) instead of any raw clock value.
#include "e2e_harness.hpp"

TEST(TimeCompileRun, Time) {
    e2e::expect_e2e("time_time", R"PURR(import io
import time
io.print(time.time() > 0.0)
)PURR", "True\n");
}

TEST(TimeCompileRun, TimeNs) {
    e2e::expect_e2e("time_time_ns", R"PURR(import io
import time
io.print(time.time_ns() > 0)
)PURR", "True\n");
}

TEST(TimeCompileRun, Monotonic) {
    e2e::expect_e2e("time_monotonic", R"PURR(import io
import time
io.print(time.monotonic() > 0.0)
)PURR", "True\n");
}

TEST(TimeCompileRun, MonotonicNs) {
    e2e::expect_e2e("time_monotonic_ns", R"PURR(import io
import time
io.print(time.monotonic_ns() > 0)
)PURR", "True\n");
}

TEST(TimeCompileRun, PerfCounter) {
    e2e::expect_e2e("time_perf_counter", R"PURR(import io
import time
io.print(time.perf_counter() > 0.0)
)PURR", "True\n");
}

TEST(TimeCompileRun, PerfCounterNs) {
    e2e::expect_e2e("time_perf_counter_ns", R"PURR(import io
import time
io.print(time.perf_counter_ns() > 0)
)PURR", "True\n");
}

TEST(TimeCompileRun, ProcessTime) {
    e2e::expect_e2e("time_process_time", R"PURR(import io
import time
io.print(time.process_time() >= 0.0)
)PURR", "True\n");
}

TEST(TimeCompileRun, Sleep) {
    // sleep blocks at least the requested duration, so the monotonic clock after
    // sleeping is >= the reading before.
    e2e::expect_e2e("time_sleep", R"PURR(import io
import time
let t0 = time.monotonic()
time.sleep(0.01)
let t1 = time.monotonic()
io.print(t1 >= t0)
)PURR", "True\n");
}
