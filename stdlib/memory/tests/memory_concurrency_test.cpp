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
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "../memory.hpp"
#include "../../ndarray/ndarray.hpp"

namespace mem = cheatah::memory;
namespace nd  = cheatah::ndarray;

namespace {
// Spawn `n` threads running `body(i)`, join all.
template <class F>
void run_threads(int n, F body) {
    std::vector<std::thread> ts;
    ts.reserve(n);
    for (int i = 0; i < n; ++i) ts.emplace_back([=] { body(i); });
    for (auto& t : ts) t.join();
}
constexpr int kWriters = 8;
constexpr int kIters   = 20'000;   // enough to expose a lost-update race, fast enough for the gate
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
TEST(MemoryConcurrency, ManyReadersCoexistThenAWriteDrains) {
    auto o = mem::own<long long>(7);
    std::atomic<int> peak{0}, active{0};
    std::atomic<bool> stop{false};
    run_threads(6, [&](int id) {
        if (id == 0) {
            for (int k = 0; k < 200; ++k) { auto w = o.rwrite().acquire(); w.write(w.read() + 1); }
            stop = true;
        } else {
            while (!stop.load()) {
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
