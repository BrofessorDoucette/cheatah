// Unit tests for the `thread` module — spawn/Thread only (shared state is the memory module's
// job and is tested there). Everything here is DETERMINISTIC: results are observed through
// join-ordering or non-copyable accumulators (a std::atomic passed by reference), never through
// timing. Iteration counts stay small — this suite also runs under Valgrind, ASan, and TSan.

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "thread.hpp"

namespace thr = cheatah::thread;

// ── spawn: runs the worker, copies copyables, references non-copyables, moves rvalues ──────

TEST(CheatahThread, SpawnRunsTheWorker) {
    std::atomic<long long> out{0};  // non-copyable -> passed by reference (the one sharing path)
    auto t = thr::spawn([](std::atomic<long long>& o, long long n) { o = n * 2; }, out, 21);
    t.join();
    EXPECT_EQ(out.load(), 42);
}

TEST(CheatahThread, SpawnRunsTheGenericLambdaLowering) {
    // The shape purrc emits for an UNTYPED cheatah fn passed by name: a capture-less generic
    // forwarding lambda. Must deduce into spawn and run.
    std::atomic<long long> out{0};
    auto generic = [](auto&&... a) { ((void)0, ..., void(a)); };
    auto t = thr::spawn(generic, 1, 2.5, std::string("x"));
    t.join();
    auto u = thr::spawn([](auto&& o, auto&& n) { o += n; }, out, 5);
    u.join();
    EXPECT_EQ(out.load(), 5);
}

TEST(CheatahThread, CopyableArgumentsAreCopied) {
    // A copyable lvalue is decay-copied into the thread: the worker's writes stay its own.
    std::string mine = "caller";
    auto t = thr::spawn([](std::string& s) { s += "-worker"; }, mine);
    t.join();
    EXPECT_EQ(mine, "caller");  // untouched — the worker mutated its own copy
}

TEST(CheatahThread, SpawnPassesANonCopyableByReference) {
    // Two workers share ONE non-copyable, non-movable object by reference — exact final state.
    std::atomic<long long> sum{0};
    {
        auto a = thr::spawn([](std::atomic<long long>& s) { for (int i = 0; i < 1000; ++i) ++s; }, sum);
        auto b = thr::spawn([](std::atomic<long long>& s) { for (int i = 0; i < 1000; ++i) ++s; }, sum);
    }  // both guards join here
    EXPECT_EQ(sum.load(), 2000);
}

TEST(CheatahThread, SpawnMovesANonCopyableRvalue) {
    // A move-only RVALUE (a factory result — e.g. a socket/io guard) is moved INTO the thread.
    std::atomic<long long> out{0};
    auto t = thr::spawn(
        [](std::atomic<long long>& o, std::unique_ptr<long long>& p) { o = *p; },
        out, std::make_unique<long long>(7));
    t.join();
    EXPECT_EQ(out.load(), 7);
}

// ── join: rethrow, one-shot, joinable lifecycle ─────────────────────────────────────────────

TEST(CheatahThread, JoinRethrowsTheWorkersException) {
    auto t = thr::spawn([] { throw std::runtime_error("kaboom"); });
    try {
        t.join();
        FAIL() << "join() must rethrow the worker's exception";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "kaboom");
    }
    EXPECT_FALSE(t.joinable());
}

TEST(CheatahThread, JoinOnNothingRaises) {
    auto t = thr::spawn([] {});
    t.join();
    EXPECT_THROW(t.join(), std::runtime_error);  // one-shot: a second join raises
}

TEST(CheatahThread, JoinableLifecycle) {
    std::atomic<bool> release{false};
    auto t = thr::spawn([](std::atomic<bool>& r) { while (!r) {} }, release);
    EXPECT_TRUE(t.joinable());
    release = true;
    t.join();
    EXPECT_FALSE(t.joinable());
}

// ── the guard: move-only ownership, join-on-destroy, honest error reporting ────────────────

TEST(CheatahThread, MoveTransfersOwnership) {
    std::atomic<long long> out{0};
    auto a = thr::spawn([](std::atomic<long long>& o) { o = 1; }, out);
    thr::Thread b = std::move(a);
    EXPECT_FALSE(a.joinable());  // NOLINT(bugprone-use-after-move) — moved-from state is the test
    EXPECT_TRUE(b.joinable());
    b.join();
    EXPECT_EQ(out.load(), 1);
}

TEST(CheatahThread, MoveAssignSettlesTheOldThread) {
    // Assigning over a guard whose worker threw must JOIN it and REPORT the unobserved error.
    testing::internal::CaptureStderr();
    auto loser = thr::spawn([] { throw std::runtime_error("lost update"); });
    std::atomic<long long> out{0};
    loser = thr::spawn([](std::atomic<long long>& o) { o = 9; }, out);
    const std::string err = testing::internal::GetCapturedStderr();
    EXPECT_NE(err.find("cheatah thread: unhandled exception in thread: lost update"),
              std::string::npos);
    loser.join();
    EXPECT_EQ(out.load(), 9);
}

TEST(CheatahThread, DestructorJoinsARunningThread) {
    std::atomic<long long> done{0};
    std::atomic<bool> release{false};
    {
        auto t = thr::spawn(
            [](std::atomic<long long>& d, std::atomic<bool>& r) {
                while (!r) {}
                d = 1;
            },
            done, release);
        release = true;
    }  // guard drops while the worker may still be running -> destructor joins
    EXPECT_EQ(done.load(), 1);
}

TEST(CheatahThread, DestructorReportsAnUnobservedException) {
    testing::internal::CaptureStderr();
    { auto t = thr::spawn([] { throw std::runtime_error("nobody joined me"); }); }
    const std::string err = testing::internal::GetCapturedStderr();
    EXPECT_NE(err.find("cheatah thread: unhandled exception in thread: nobody joined me"),
              std::string::npos);
}

TEST(CheatahThread, DestructorReportsANonStdException) {
    // A worker can escape with anything; without a `what()` the report says so honestly.
    testing::internal::CaptureStderr();
    { auto t = thr::spawn([] { throw 42; }); }
    const std::string err = testing::internal::GetCapturedStderr();
    EXPECT_NE(err.find("cheatah thread: unhandled exception in thread: unknown error"),
              std::string::npos);
}

// ── compile-time contract: the concepts reject what must not compile ────────────────────────

TEST(CheatahThread, ConceptsRejectUnholdableArguments) {
    struct Pinned {  // non-copyable, non-movable — the memory::Owner shape
        Pinned() = default;
        Pinned(const Pinned&) = delete;
        Pinned& operator=(const Pinned&) = delete;
    };
    static_assert(thr::SpawnArg<Pinned&>, "a pinned lvalue travels by reference");
    static_assert(!thr::SpawnArg<Pinned>, "a pinned TEMPORARY would dangle — must not compile");
    static_assert(thr::SpawnArg<int>, "values are copied in");
    static_assert(thr::SpawnArg<std::unique_ptr<int>>, "a movable rvalue is moved in");
    static_assert(thr::SpawnCallable<void (*)(long long), int>,
                  "the fully-typed fn-pointer lowering is spawnable");
    static_assert(!thr::SpawnCallable<void (*)(long long)>,
                  "arity mismatches are rejected at compile time");
    SUCCEED();
}
