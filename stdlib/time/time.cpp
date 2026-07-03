// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "time.hpp"

#include <chrono>
#include <ctime>
#include <thread>

namespace cheatah::time {

namespace {
using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::nanoseconds;
using std::chrono::steady_clock;
using std::chrono::system_clock;
} // namespace

double time() { return duration<double>(system_clock::now().time_since_epoch()).count(); }
long long time_ns() {
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

double monotonic() { return duration<double>(steady_clock::now().time_since_epoch()).count(); }
long long monotonic_ns() {
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

// steady_clock is the highest-resolution monotonic clock the platform offers.
double perf_counter() { return duration<double>(steady_clock::now().time_since_epoch()).count(); }
long long perf_counter_ns() {
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

double process_time() { return static_cast<double>(std::clock()) / CLOCKS_PER_SEC; }

void sleep(double seconds) { std::this_thread::sleep_for(duration<double>(seconds)); }

} // namespace cheatah::time
