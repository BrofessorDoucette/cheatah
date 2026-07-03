// System-level e2e for the `thread` module (suite StdlibE2E, like every module's *_sys_test): one
// program driving the whole surface — spawn with mixed argument types, sequential join chains,
// the `with` guard, joinable(), and a worker `raise` recovered in-language with try/except (the
// per-venue retry shape a real multi-feed collector uses). Deterministic by construction: a
// worker's output is only observed behind its join.
//
// ThreadOwnerParamLowering asserts the GENERATED C++ (not just behavior): a worker parameter
// typed `memory.Owner<int>` must lower to a concrete `Owner<long long>&` — the same type
// `memory.own(0)` deduces — so a pinned Owner travels into a thread by reference. It only
// asserts the signature (the program never exercises the Owner engine, which is TDD elsewhere).
#include <string>

#include "e2e_harness.hpp"

TEST(StdlibE2E, Thread) {
    e2e::expect_e2e("thread_sys", R"PURR(import io
import thread

fn double_and_report(n : int, tag : str) {
    io.print(tag, n * 2)
}

fn flaky(attempt : int) {
    if attempt < 2 {
        raise "feed dropped"
    }
    io.print("feed up on attempt", attempt)
}

fn main() {
    # spawn -> join, one at a time (deterministic output ordering)
    let t1 = thread.spawn(double_and_report, 21, "first:")
    t1.join()
    io.print(t1.joinable())

    # the `with` guard joins on scope exit
    with thread.spawn(double_and_report, 100, "second:") {
    }

    # per-worker retry: a raise re-surfaces at join(); the supervisor retries (collector shape)
    let attempt = 1
    let up = false
    while not up {
        let w = thread.spawn(flaky, attempt)
        try {
            w.join()
            up = true
        } except e {
            io.print("retrying after:", e)
            attempt = attempt + 1
        }
    }
    io.print("collector done")
}

main()
)PURR",
                    "first: 42\nFalse\nsecond: 200\n"
                    "retrying after: feed dropped\nfeed up on attempt 2\ncollector done\n");
}

TEST(StdlibE2E, ThreadOwnerParamLowering) {
    const std::string gen = e2e::expect_e2e_source("thread_owner_param", R"PURR(import io
import memory
import thread

fn worker(o : memory.Owner<int>, n : int) {
    io.print("worker sees", n)
}

fn main() {
    let o = memory.own(0)
    let t = thread.spawn(worker, o, 7)
    t.join()
}

main()
)PURR",
                                                  "worker sees 7\n");
    // The typed Owner parameter lowers to the CONCRETE reference memory.own(0) deduces —
    // `int` inside the template argument maps like every other cheatah int.
    EXPECT_NE(gen.find("memory::Owner<long long>& o"), std::string::npos)
        << "expected the Owner<int> parameter to lower to memory::Owner<long long>&:\n"
        << gen;
    EXPECT_EQ(gen.find("Owner<int>"), std::string::npos)
        << "raw C++ `Owner<int>` must not appear (cheatah int is long long):\n"
        << gen;
}

TEST(StdlibE2E, ThreadSharedOwnerSum) {
    // The two-module design, end to end: N spawned workers share ONE pinned Owner (by reference)
    // and increment through exclusive write leases — the drain-before-write engine makes the total
    // exact regardless of interleaving. This is the .purr the thread module exists for.
    e2e::expect_e2e("thread_owner_sum", R"PURR(import io
import memory
import thread

fn worker(o : memory.Owner<int>, quota : int) {
    let done = 0
    while done < quota {
        with o.rwrite().acquire() as w {
            w.write(w.read() + 1)
        }
        done = done + 1
    }
}

fn main() {
    let o = memory.own(0)
    with thread.spawn(worker, o, 2000) {
        with thread.spawn(worker, o, 2000) {
            with thread.spawn(worker, o, 2000) {
            }
        }
    }
    io.print(o.rread().acquire().read())
}

main()
)PURR",
                    "6000\n");
}
