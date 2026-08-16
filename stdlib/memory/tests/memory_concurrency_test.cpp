// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// GOLD adversarial CONCURRENCY suite for the `memory` module (suite MemoryConcurrency).
//
// ┌───────────────────────────────────────────────────────────────────────────────────────────┐
// │  TDD-RED until the Owner scheduling engine is implemented. These use REAL std::thread over a │
// │  shared Owner, so they cannot even be *linked* (Owner::rread/rwrite are declared-only) — and  │
// │  an exception thrown inside a spawned thread would std::terminate the runner. They are        │
// │  therefore built ONLY behind the CMake option CHEATAH_BUILD_MEMORY_TESTS (default OFF), which  │
// │  we flip ON as we build the engine (red → green). See stdlib/memory/tests/README.md.          │
// └───────────────────────────────────────────────────────────────────────────────────────────┘
//
// Design principle (per the user's ask): drive the object from many threads in a way whose *final*
// state is DETERMINISTIC even though the interleaving is not. We never do "a bunch of rotations and
// expect the same result"; we use commuting/exclusive updates and interleaving-invariant predicates,
// so a correct engine ALWAYS yields the exact expected answer and a broken one (lost updates, torn
// reads, missed drains, non-exclusive writes) is caught deterministically.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "../memory.hpp"
#include "../../ndarray/ndarray.hpp"

namespace mem = cheatah::memory;
namespace nd  = cheatah::ndarray;

#if __has_include(<valgrind/valgrind.h>)
#include <valgrind/valgrind.h>
#define CHEATAH_HAVE_VALGRIND_H 1
#endif

namespace {

/// Are we running under valgrind (helgrind, drd, memcheck)?
///
/// It matters because those tools serialize threads, so a test whose subject IS concurrency cannot
/// observe its own property under them. `RUNNING_ON_VALGRIND` is valgrind's own documented client
/// request and costs nothing when absent; the `__has_include` guard keeps the header optional so a
/// machine without valgrind-dev still builds.
/// @return true iff running under a valgrind tool.
/// @complexity O(1). @alloc none.
[[nodiscard]] bool running_under_valgrind() noexcept {
#ifdef CHEATAH_HAVE_VALGRIND_H
    return RUNNING_ON_VALGRIND != 0;
#else
    return false;
#endif
}

/**
 * RANDOM LATENCY INJECTION — off by default, on with `CHEATAH_MEMORY_JITTER=<max_microseconds>`.
 *
 * WHY, given the suite already passes: a passing concurrency test proves the interleavings the
 * scheduler HAPPENED to pick were fine. It says nothing about the ones it never picked. Two real
 * flakes in this module were found only when something perturbed timing — one by machine load, one
 * by ThreadSanitizer's slowdown — which means timing perturbation was doing the finding, accidentally
 * and unrepeatably. This makes it deliberate and repeatable.
 *
 * REPRODUCIBILITY IS THE POINT. The seed comes from `CHEATAH_MEMORY_SEED` when set; otherwise one is
 * drawn and PRINTED, so a failure found by a random run can be replayed exactly. A fuzzer whose
 * failures cannot be reproduced is a rumour generator.
 *
 * Per-thread state, so threads do not contend on the generator and thereby add a synchronisation
 * point that changes the very interleaving being explored.
 */
[[nodiscard]] unsigned jitter_max_us() {
    static const unsigned v = [] {
        const char* e = std::getenv("CHEATAH_MEMORY_JITTER");
        return e ? static_cast<unsigned>(std::strtoul(e, nullptr, 10)) : 0u;
    }();
    return v;
}

[[nodiscard]] unsigned jitter_seed() {
    static const unsigned s = [] {
        if (const char* e = std::getenv("CHEATAH_MEMORY_SEED"))
            return static_cast<unsigned>(std::strtoul(e, nullptr, 10));
        const auto drawn = static_cast<unsigned>(std::random_device{}());
        if (jitter_max_us() != 0)
            std::fprintf(stderr, "[memory-jitter] seed=%u (replay: CHEATAH_MEMORY_SEED=%u)\n",
                         drawn, drawn);
        return drawn;
    }();
    return s;
}

/// Sleep a random sub-window, or yield. No-op unless jitter is enabled.
void jitter() {
    const unsigned max_us = jitter_max_us();
    if (max_us == 0) return;
    static thread_local std::mt19937 rng{jitter_seed() ^
                                         static_cast<unsigned>(
                                             std::hash<std::thread::id>{}(std::this_thread::get_id()))};
    std::uniform_int_distribution<unsigned> d(0, max_us);
    const unsigned us = d(rng);
    if (us == 0) std::this_thread::yield();
    else         std::this_thread::sleep_for(std::chrono::microseconds(us));
}

// Spawn `n` threads running `body(i)`, join all.
template <class F>
void run_threads(int n, F body) {
    std::vector<std::thread> ts;
    ts.reserve(n);
    for (int i = 0; i < n; ++i) ts.emplace_back([=] { body(i); });
    for (auto& t : ts) t.join();
}
constexpr int kWriters = 8;

/// Iterations per writer: enough to expose a lost update, fast enough for the gate.
///
/// SCALED DOWN UNDER VALGRIND, and that is what makes a helgrind lane possible at all. Helgrind
/// instruments every memory access and every lock operation at roughly 100x, so 8 x 20,000
/// acquisitions is minutes per test and the whole suite does not finish inside any sane timeout —
/// measured, not guessed. The race conditions these tests hunt are not made more likely by volume
/// under a tool that already serializes and inspects every interleaving; volume is how we buy
/// coverage from a NATIVE scheduler, which is a different lane. So: full count natively and under
/// TSan, a small count under valgrind, and the reduction is announced rather than silent.
const int kIters = [] {
    const int n = running_under_valgrind() ? 300 : 20'000;
    if (running_under_valgrind())
        std::fprintf(stderr, "[memory] valgrind detected: kIters reduced to %d for tractability\n", n);
    return n;
}();
}  // namespace

// ── 1. Exclusive writes never lose an update: 8×N increments == 8N, exactly ───────────────
TEST(MemoryConcurrency, ManyWritersDeterministicSum) {
    auto o = mem::own<long long>(0);
    run_threads(kWriters, [&](int) {
        for (int k = 0; k < kIters; ++k) {
            auto w = o.rwrite().acquire();     // exclusive: read-modify-write cannot interleave
            w.write(w.read() + 1);
        }
    });
    EXPECT_EQ(o.rread().acquire().read(),
              static_cast<long long>(kWriters) * kIters);   // == 400000, deterministic
}

// ── 2. Owner<ndarray>: readers NEVER see a torn (non-uniform) array; final is exact ───────
// The array starts uniform (all equal). Every writer adds 1 to *every* element under one write
// lease, so under a correct exclusive lease the array is uniform at every quiescent point. Readers
// assert uniformity — the value they see varies (nondeterministic), but "all elements equal" must
// ALWAYS hold. A missed drain / non-exclusive write would let a reader observe a half-updated array.
TEST(MemoryConcurrency, OwnerOfNdArrayStaysUniformAndSumsExactly) {
    constexpr std::size_t N = 256;
    auto o = mem::own(nd::basic_ndarray<long long>({N}, 0));
    std::atomic<bool> torn{false};
    std::atomic<bool> stop{false};

    // 4 reader threads: continuously assert the array is uniform.
    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&] {
            while (!stop.load()) {
                auto lease = o.rread().acquire();
                const auto& a = lease.read();
                const long long first = a.at({0});               // const read: at() (operator[] is non-const)
                for (std::size_t i = 1; i < a.size(); ++i)
                    if (a.at({i}) != first) { torn = true; return; }
            }
        });
    }
    // Writers: each adds 1 to every element, kIters/50 times.
    const int rounds = kIters / 50;
    run_threads(kWriters, [&](int) {
        for (int k = 0; k < rounds; ++k) {
            auto w = o.rwrite().acquire();
            const std::size_t n = w.read().size();
            for (std::size_t i = 0; i < n; ++i) w.write(i, w.read().at({i}) + 1);  // indexed setter
        }
    });
    stop = true;
    for (auto& t : readers) t.join();

    EXPECT_FALSE(torn.load()) << "a reader observed a partially-updated (non-uniform) array";
    auto final = o.rread().acquire();
    const long long expected = static_cast<long long>(kWriters) * rounds;
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(final.read().at({i}), expected);               // const read via at()
}

// ── 3. Readers never see a torn write of a multi-field invariant (sum == a + b) ───────────
TEST(MemoryConcurrency, ReadersNeverSeeATornWrite) {
    struct Triple { long long a = 0, b = 0, sum = 0; };
    auto o = mem::own(Triple{});
    std::atomic<bool> torn{false}, stop{false};

    std::thread reader([&] {
        while (!stop.load()) {
            auto r = o.rread().acquire();
            const Triple& t = r.read();
            if (t.sum != t.a + t.b) { torn = true; return; }   // invariant must hold at every read
        }
    });
    run_threads(kWriters, [&](int id) {
        for (int k = 0; k < kIters; ++k) {
            const long long a = id * 1000 + k, b = k;
            auto w = o.rwrite().acquire();
            w.write(Triple{a, b, a + b});                       // set all three at once (one exclusive write)
        }
    });
    stop = true;
    reader.join();
    EXPECT_FALSE(torn.load()) << "a reader observed a half-written Triple (sum != a + b)";
}

// ── 4. Immediate-write preempts under load; its effect lands; normal writers still finish ─
// Adds commute, so the final total is deterministic regardless of WHEN the immediate-write fires.
TEST(MemoryConcurrency, ImmediateWriteLandsUnderLoad) {
    auto o = mem::own<long long>(0);
    std::atomic<bool> go{false};
    std::thread emergency([&] {
        while (!go.load()) std::this_thread::yield();
        auto w = o.rwrite<mem::immediate>().acquire();          // jumps the queue, preempts active writer
        w.write(w.read() + 1'000'000);
    });
    go = true;
    run_threads(kWriters, [&](int) {
        for (int k = 0; k < kIters; ++k) { auto w = o.rwrite().acquire(); w.write(w.read() + 1); }
    });
    emergency.join();
    EXPECT_EQ(o.rread().acquire().read(),
              static_cast<long long>(kWriters) * kIters + 1'000'000);
}

// ── 5. A thread that reads then writes in a loop must never self-deadlock ──────────────────
TEST(MemoryConcurrency, ReadThenWriteLoopNoDeadlock) {
    auto o = mem::own<long long>(0);
    run_threads(kWriters, [&](int) {
        for (int k = 0; k < kIters / 5; ++k) {
            { auto r = o.rread().acquire();  (void)r.read(); }  // read, release
            { auto w = o.rwrite().acquire(); w.write(w.read() + 1); }  // then write — must not wait on self
        }
    });
    EXPECT_EQ(o.rread().acquire().read(), static_cast<long long>(kWriters) * (kIters / 5));
}

// ── 6. Renewal across relocation: writers grow a string (reallocating), readers never dangle ─
TEST(MemoryConcurrency, RenewalAcrossRelocationNeverDangles) {
    auto o = mem::own(std::string("a"));
    std::atomic<bool> stop{false}, corrupt{false};
    std::thread reader([&] {
        while (!stop.load()) {
            auto r = o.rread().acquire();
            const std::string& s = r.read();                    // must point at the CURRENT buffer
            for (char c : s) if (c != 'a') { corrupt = true; return; }  // every byte is 'a', never garbage
        }
    });
    for (int k = 0; k < 2000; ++k) { auto w = o.rwrite().acquire(); w.write(w.read() + "a"); }  // grows/relocates
    stop = true;
    reader.join();
    EXPECT_FALSE(corrupt.load()) << "a renewed reader saw freed/garbage bytes after a relocation";
    EXPECT_EQ(o.rread().acquire().read().size(), 2001u);
}

// ── 7. Lease churn: hammer create/destroy so ASan/Valgrind (gate stages) can catch a leak ─
TEST(MemoryConcurrency, LeaseChurnLeakHunt) {
    auto o = mem::own<long long>(0);
    run_threads(kWriters, [&](int) {
        for (int k = 0; k < kIters; ++k) {
            if (k & 1) { auto r = o.rread().acquire();  (void)r.read(); }
            else       { auto w = o.rwrite().acquire(); w.write(w.read() + 1); }
        }
    });
    // Value check is secondary; the real assertion is "no bytes leaked" under ASan/Valgrind.
    EXPECT_GE(o.rread().acquire().read(), 0);
}

// ── 8. Concurrent readers coexist (shared), and a write still drains them ─────────────────
// `peak > 1` is an EMERGENT property, and this test used to simply hope for it: the writer fired 200
// back-to-back writes and set `stop`, and whether any two readers ever overlapped was left to the
// scheduler. Under ThreadSanitizer — which the QA gate runs — thread start-up is slow enough that the
// writer regularly finished before the readers got going at all, so `peak` stayed 0 and this failed
// about one run in three. The property is real; it just has to be ARRANGED rather than wished for.
//
// Two changes, and both are about giving the property a chance instead of asserting it blindly:
//   1. a start gate, so every reader is inside its loop before the writer begins; and
//   2. the writer does not end the run until an overlap has actually been observed (bounded).
//
// The start gate is taken BEFORE any lease is acquired. That ordering is load-bearing: readers that
// waited on each other while HOLDING read leases would block the writer's drain, and the drain is
// what they would be waiting on — a circular wait, which is a deadlock rather than a flake.
TEST(MemoryConcurrency, ManyReadersCoexistThenAWriteDrains) {
    using clock = std::chrono::steady_clock;
    constexpr int  kReaders  = 5;
    constexpr auto kPatience = std::chrono::seconds(5);

    // SKIPPED UNDER VALGRIND, and this is a tool limit rather than a weakness in the assertion.
    // Helgrind and Memcheck run threads ONE AT A TIME — that serialization is how they get a
    // consistent view — so two read leases can never be live simultaneously and `peak` is pinned at
    // 1 by construction. Every other test in this file measures a FINAL state and is therefore
    // meaningful serialized; this one measures concurrency itself, which is the one thing a
    // serializing tool cannot show. Skipped with the reason stated, never quietly weakened to
    // `>= 1` — an assertion that passes under the tool by no longer testing anything is worse than
    // one that admits it did not run.
    if (running_under_valgrind())
        GTEST_SKIP() << "valgrind serializes threads; overlapping read leases are unobservable. "
                        "Run this lane natively or under ThreadSanitizer, which models atomics.";

    auto o = mem::own<long long>(7);
    std::atomic<int>  peak{0}, active{0}, ready{0};
    std::atomic<bool> stop{false};
    run_threads(kReaders + 1, [&](int id) {
        if (id == 0) {
            while (ready.load() < kReaders) std::this_thread::yield();   // readers are running
            for (int k = 0; k < 200; ++k) {
                jitter();
                auto w = o.rwrite().acquire();
                jitter();
                w.write(w.read() + 1);
            }
            // Hold the run open until the readers have demonstrably shared the object. Bounded, so a
            // real regression — reads that serialize behind each other — FAILS on the assertion
            // below instead of hanging the suite.
            const auto deadline = clock::now() + kPatience;
            while (peak.load() <= 1 && clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            stop = true;
        } else {
            ++ready;
            while (!stop.load()) {
                jitter();
                auto r = o.rread().acquire();
                int a = ++active;
                int seen = peak.load();
                while (a > seen && !peak.compare_exchange_weak(seen, a)) {}
                std::this_thread::yield();
                --active;
                EXPECT_GE(r.read(), 7);                          // monotonic: writes only increment
            }
        }
    });
    EXPECT_GT(peak.load(), 1) << "read leases should have coexisted (shared), not serialized";
    EXPECT_EQ(o.rread().acquire().read(), 207);
}
