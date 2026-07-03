// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Compile-run unit tests for the `thread` module: one test per purr-callable entry point
// (spawn, Thread.join, Thread.joinable). Each writes a tiny .purr, compiles it with purrc, runs
// it under the cheatah runtime, and asserts the EXACT stdout — so every program is deterministic:
// a worker's output is only observed after its join (or the `with` guard's scope exit), never
// raced against the main thread's. Complements the in-process unit tests
// (stdlib/tests/thread_test.cpp) and the per-module system test (StdlibE2E.Thread).
#include "e2e_harness.hpp"

TEST(ThreadCompileRun, SpawnJoin) {
    e2e::expect_e2e("thread_spawn_join", R"PURR(import io
import thread

fn worker(n : int, tag : str) {
    io.print(tag, n * 2)
}

let t = thread.spawn(worker, 21, "spawned:")
t.join()
io.print("joined")
)PURR",
                    "spawned: 42\njoined\n");
}

TEST(ThreadCompileRun, Joinable) {
    e2e::expect_e2e("thread_joinable", R"PURR(import io
import thread

fn worker() {
    io.print("ran")
}

let t = thread.spawn(worker)
io.print(t.joinable())
t.join()
io.print(t.joinable())
)PURR",
                    "True\nran\nFalse\n");
}

TEST(ThreadCompileRun, JoinCatchesWorkerRaise) {
    // A worker's `raise` does not kill the program: it re-surfaces at join(), catchable in-language.
    e2e::expect_e2e("thread_join_raise", R"PURR(import io
import thread

fn fails() {
    raise "boom from worker"
}

let t = thread.spawn(fails)
try {
    t.join()
} except e {
    io.print("caught:", e)
}
io.print("alive")
)PURR",
                    "caught: boom from worker\nalive\n");
}

TEST(ThreadCompileRun, WithGuardJoinsAtScopeExit) {
    // The RAII story: a Thread held by `with` joins on scope exit, so the worker's line is
    // guaranteed to precede anything printed after the block.
    e2e::expect_e2e("thread_with_guard", R"PURR(import io
import thread

fn worker(tag : str) {
    io.print("worker", tag)
}

with thread.spawn(worker, "A") {
}
io.print("after")
)PURR",
                    "worker A\nafter\n");
}

TEST(ThreadCompileRun, SpawnForwardsTheCollectorShapedSignature) {
    // The signature shape the module exists for: several plain-value args (str/int/float/bool),
    // each COPIED into its thread; threads joined one at a time so stdout stays deterministic.
    e2e::expect_e2e("thread_collector_shape", R"PURR(import io
import thread

fn collect(venue : str, product : str, depth : int, funding : bool) {
    io.print(venue, product, depth, funding)
}

let a = thread.spawn(collect, "alpha", "X-Y", 10, false)
a.join()
let b = thread.spawn(collect, "beta", "Y-Z", 20, true)
b.join()
)PURR",
                    "alpha X-Y 10 False\nbeta Y-Z 20 True\n");
}
