# cheatah `time`

High-accuracy timing built on C++ `<chrono>` clocks (`system_clock` for wall-clock,
`steady_clock` for monotonic counters).

```purr
import time

start = time.perf_counter()
time.sleep(0.5)
elapsed = time.perf_counter() - start
```

## Functions

Wall-clock (since the Unix epoch):

- `time()` — seconds as a `double`.
- `time_ns()` — nanoseconds as a `long long`.

Monotonic (never runs backwards):

- `monotonic()` / `monotonic_ns()` — monotonic seconds / nanoseconds.
- `perf_counter()` / `perf_counter_ns()` — highest-resolution monotonic counter.

Other:

- `process_time()` — CPU time consumed by this process, in seconds.
- `sleep(seconds)` — suspend the current thread for a (fractional) duration.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[time.hpp](time.hpp). Tested in [../tests/time_test.cpp](../tests/time_test.cpp);
ASan + Valgrind clean via the QA gate (`security/run-valgrind.sh`).
