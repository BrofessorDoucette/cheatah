// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// SPEC (for review) — the intended behaviour of the `memory` module. NOT wired into the build and
// NOT expected to pass until the C++ backend engine is implemented; every test is a promise the
// implementation must keep.
//
// Types (namespace cheatah::memory; PascalCase handles, lowercase tags/enums — matches File/Conn/
// Pattern vs read/write):
//   Owner<T>                          — the sole owner + coordinator (non-copyable, pinned). `T` is the
//                                       ONLY class template arg. Policy is a CONSTRUCTOR arg; priority
//                                       is a compile-time arg on the write accessor.
//   Lease<T, read | write>            — the only handle; read()=get, write(v)=set, write()=in-place ref (NOT get()).
//   Request<Lease>                    — what accessors return; `.acquire(on_interrupt)` BLOCKS -> Lease.
// Access (EVERY accessor returns a Request for a lease, never a bare lease — a read included):
//   o.rread()            -> Request<Lease<T, read>>.  r = ....acquire(). r.read() -> const T&.
//                           r.valid()/r.expired() track the owner's stop request.
//   o.rwrite<priority>() -> Request<Lease<T, write>>. w = ....acquire(). w.write(value) sets; w.write() -> T& for in-place. Exclusive.
//                           priority is a compile-time int or the caller's enum value; higher = higher;
//                           NEGATIVE (memory::immediate == -1) = immediate-write (preempt + resume).
//   own(value[, policy]) -> Owner<T>. policy is memory::interleave (default) | memory::writes_first.
//   Request::acquire(on_interrupt) — blocks for the lease; the optional callback is wired to the
//                                    lease's stop token and fires when the owner needs the lease back.
// Safety: a write touches no byte until every read lease has released (drain-before-write); a reader
// that re-acquires after a write sees the object's CURRENT location (no dangling).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "memory.hpp"

namespace mem = cheatah::memory;
using namespace std::chrono_literals;

namespace { struct Point { int x = 0, y = 0; }; }

// ── the core invariant: no accessor ever returns a bare lease ─────────────────────────────

TEST(Memory, EveryAccessorReturnsARequestNotABareLease) {
    auto o = mem::own(Point{});
    static_assert(std::is_same_v<decltype(o.rread()),
                                 mem::Request<mem::Lease<Point, mem::read>>>,
                  "rread() hands back a Request for a read lease, not a read lease");
    static_assert(std::is_same_v<decltype(o.rwrite()),
                                 mem::Request<mem::Lease<Point, mem::write>>>,
                  "rwrite() hands back a Request for a write lease, not a write lease");
    // .acquire() is how a Request redeems into the lease.
    static_assert(std::is_same_v<decltype(o.rread().acquire()), mem::Lease<Point, mem::read>>,
                  "Request::acquire() yields the lease");
    SUCCEED();
}

// read() must hand back a REFERENCE to the owned object — never a copy, never a raw pointer — so a
// caller touches the object in place (and a big object isn't copied on every read).
TEST(Memory, ReadReturnsAReferenceNotACopyOrPointer) {
    struct Big { int a[64]; };
    using RB = mem::Lease<Big, mem::read>;
    static_assert(std::is_same_v<decltype(std::declval<const RB&>().read()), const Big&>,
                  "read() returns const T& — a reference to the owned object, not a copy or a T*");
    static_assert(std::is_reference_v<decltype(std::declval<const RB&>().read())>,
                  "read() returns a reference");
    static_assert(!std::is_pointer_v<std::remove_reference_t<decltype(std::declval<const RB&>().read())>>,
                  "read() does not return a raw pointer");
    static_assert(std::is_void_v<decltype(std::declval<mem::Lease<Big, mem::write>&>().write(std::declval<Big>()))>,
                  "write(value) is a SETTER — it returns void, never an object/reference");
    SUCCEED();
}

// ── ownership basics ─────────────────────────────────────────────────────────────────────

TEST(Memory, OwnerIsSoleAndNonCopyable) {
    static_assert(!std::is_copy_constructible_v<mem::Owner<Point>>, "owner is the sole owner");
    static_assert(!std::is_copy_assignable_v<mem::Owner<Point>>,    "owner is the sole owner");
    SUCCEED();
}

TEST(Memory, ObjectDiesWithOwner) {
    static int live = 0;
    // std::movable (own<T> constrains on it): ctors count, assignments are no-ops for the count.
    struct T {
        T(){++live;} T(const T& /*unused*/){++live;} T(T&& /*unused*/) noexcept {++live;}
        // NOLINT below: this probe's operator= is DELIBERATELY a no-op either way — only
        // construction/destruction move the live count, so self-assignment is trivially safe.
        T& operator=(const T& /*unused*/)= default; T& operator=(T&& /*unused*/) noexcept {return *this;}  // NOLINT(cert-oop54-cpp)
        ~T(){--live;}
    };
    { auto o = mem::own(T{}); EXPECT_EQ(live, 1); }
    EXPECT_EQ(live, 0);
}

// The object is MOVED into the Owner (consumed), never copied; copying an Owner is forbidden.
TEST(Memory, OwnerConsumesAndMovesTheObjectInNeverCopies) {
    static_assert(!std::is_copy_constructible_v<mem::Owner<int>>, "Owner is non-copyable");
    static_assert(!std::is_move_constructible_v<mem::Owner<int>>, "Owner is pinned (non-movable)");

    struct Tracker {
        int moves = 0, copies = 0;
        std::shared_ptr<int> resource = std::make_shared<int>(7);  // a movable resource we can watch
        Tracker() = default;
        Tracker(const Tracker& o) : moves(o.moves), copies(o.copies + 1), resource(o.resource) {}
        Tracker(Tracker&& o) noexcept
            : moves(o.moves + 1), copies(o.copies), resource(std::move(o.resource)) {}
        Tracker& operator=(const Tracker&) = default;
        Tracker& operator=(Tracker&&) noexcept = default;
        ~Tracker() = default;
    };

    Tracker src;                          // an lvalue we hand over
    auto o = mem::own(std::move(src));    // consume it — Owner<Tracker>(Tracker&&)
    auto r = o.rread().acquire();
    EXPECT_EQ(r.read().copies, 0) << "the object must be MOVED into the Owner, never copied";
    EXPECT_GE(r.read().moves, 1) << "the object must be moved in";
    EXPECT_EQ(*r.read().resource, 7);     // the Owner holds the resource
    EXPECT_EQ(src.resource, nullptr) << "the source was consumed — its resource moved out";  // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move): moved-from state is the assertion
}

// For complex objects, each write form reaches the RIGHT item: index → the right element, key → the
// right entry, whole-value → a clean replacement; everything else stays untouched.
TEST(Memory, LeasesModifyTheCorrectItemsOfComplexObjects) {
    {   // sequence: the indexed setter hits exactly one element; the symmetric read getters read it back.
        auto o = mem::own(std::vector<int>{10, 20, 30, 40});
        { auto w = o.rwrite().acquire(); w.write(std::size_t{2}, 99); }
        auto r = o.rread().acquire();
        EXPECT_EQ(r.read(std::size_t{0}), 10);   // read(index) mirrors write(index, value)
        EXPECT_EQ(r.read(std::size_t{2}), 99);   // only index 2 changed
        EXPECT_EQ(r.read_front(), 10);           // read_front / read_back convenience
        EXPECT_EQ(r.read_back(), 40);
        EXPECT_EQ(r.read().size(), 4u);          // read() still gives the whole object
    }
    {   // mapping: keyed setter updates/inserts; read(key) mirrors it (and throws on a missing key).
        auto o = mem::own(std::map<std::string, int>{{"a", 1}, {"b", 2}});
        { auto w = o.rwrite().acquire(); w.write(std::string("b"), 22); }   // update
        { auto w = o.rwrite().acquire(); w.write(std::string("c"), 3); }    // insert
        auto r = o.rread().acquire();
        EXPECT_EQ(r.read(std::string("a")), 1);   // read(key) mirrors write(key, value)
        EXPECT_EQ(r.read(std::string("b")), 22);
        EXPECT_EQ(r.read(std::string("c")), 3);
        EXPECT_EQ(r.read().size(), 3u);
    }
    {   // nested struct: whole-value setter replaces it; the read sees the new fields.
        struct Inner { int x; std::string name; };
        auto o = mem::own(Inner{1, "old"});
        { auto w = o.rwrite().acquire(); w.write(Inner{42, "new"}); }
        auto r = o.rread().acquire();
        EXPECT_EQ(r.read().x, 42);
        EXPECT_EQ(r.read().name, "new");
    }
}

TEST(Memory, PolicyIsAConstructorArgumentNotATemplateParameter) {
    auto fair  = mem::own(0);                          // default policy: memory::interleave
    auto drain = mem::own(0, mem::writes_first);       // policy chosen at construction
    // Both are the same TYPE (Owner<int>) — policy is a stored value, not part of the type.
    static_assert(std::is_same_v<decltype(fair), decltype(drain)>,
                  "Owner<T> carries only T; policy does not template the class");
    SUCCEED();
}

// ── read leases: coexist, and are accessed via read() (never get()) ──────────────────────

TEST(Memory, ReadLeasesCoexist) {
    auto o = mem::own(Point{3, 4});
    auto r1 = o.rread().acquire();            // request -> acquire -> Lease<Point, read>
    auto r2 = o.rread().acquire();            // a second read lease at the same time — allowed
    EXPECT_TRUE(r1.valid());
    EXPECT_TRUE(r2.valid());
    EXPECT_EQ(r1.read().x, 3);                // lease access is read()/write(), never get()
    EXPECT_EQ(r2.read().y, 4);
}

TEST(Memory, ReadLeaseValidUntilAWriterNeedsIn) {
    auto o = mem::own(Point{1, 1});
    std::atomic<bool> reader_saw_expired{false}, reader_holding{false};
    std::thread reader([&] {
        auto r = o.rread().acquire();
        reader_holding = true;
        while (r.valid()) std::this_thread::yield();   // read until the owner asks us to stop
        reader_saw_expired = r.expired();              // we left the loop because the lease expired
    });                                                // reader releases here
    while (!reader_holding) std::this_thread::yield();
    { auto w = o.rwrite().acquire(); auto p = w.read(); p.x = 9; w.write(p); }  // write expires the read lease
    reader.join();
    EXPECT_TRUE(reader_saw_expired);                   // the reader observed expired()
    EXPECT_EQ(o.rread().acquire().read().x, 9);        // the write landed
}

TEST(Memory, InterruptCallbackFiresWhenTheOwnerNeedsTheLeaseBack) {
    // The callback passed INTO acquire() is the requester's "what to do if interrupted" — the owner
    // decides when; the requester only suggests the reaction.
    auto o = mem::own(0);
    std::atomic<bool> asked_to_yield{false}, reader_holding{false};
    std::thread reader([&] {
        auto r = o.rread().acquire([&] { asked_to_yield = true; });  // wired to the lease's stop token
        reader_holding = true;
        while (r.valid()) std::this_thread::yield();
    });
    while (!reader_holding) std::this_thread::yield();
    { auto w = o.rwrite().acquire(); w.write(42); }  // requesting the write trips the reader's stop
    reader.join();
    EXPECT_TRUE(asked_to_yield);                        // the interrupt handler fired
}

// ── drain-before-write: the writer waits until every reader has released ──────────────────

TEST(Memory, WriteWaitsForReadersToDrain) {
    auto o = mem::own<long long>(0);
    std::atomic<bool> reader_released{false};
    std::atomic<bool> write_began_before_release{false};
    std::thread reader([&] {
        auto r = o.rread().acquire();
        while (r.valid()) std::this_thread::sleep_for(1ms);   // hold briefly, honoring the stop
        std::this_thread::sleep_for(5ms);
        reader_released = true;
    });                                                        // release here
    std::this_thread::sleep_for(1ms);
    {
        auto w = o.rwrite().acquire();                         // must block until the reader released
        if (!reader_released) write_began_before_release = true;
        w.write(1);
    }
    reader.join();
    EXPECT_FALSE(write_began_before_release);                  // no byte moved while a reader held on
    EXPECT_EQ(o.rread().acquire().read(), 1);
}

// ── renewal: a reader re-acquiring after a write sees the NEW value at the CURRENT location ─

TEST(Memory, ReaderRenewsAndSeesTheNewValueEvenIfMoved) {
    // A std::string can reallocate (move its bytes) when it grows — the renewed read lease must
    // still be valid, pointing at the object's current location.
    auto o = mem::own(std::string("x"));
    std::atomic<bool> go{false};
    std::string seen;
    std::thread reader([&] {
        while (!go) std::this_thread::yield();
        for (;;) {
            auto r = o.rread().acquire();                      // re-request (renew); blocks behind a write
            if (r.read().size() > 100) { seen = r.read(); break; }
        }
    });
    std::thread writer([&] {
        while (!go) std::this_thread::yield();
        auto w = o.rwrite().acquire();
        w.write(std::string(500, 'a'));                        // grows -> may relocate the buffer
    });
    go = true;
    reader.join();
    writer.join();
    EXPECT_EQ(seen, std::string(500, 'a'));                    // renewed reader saw the new bytes safely
}

// ── writer-may-be-a-reader: releasing your read then writing must not deadlock ────────────

TEST(Memory, AReaderCanBecomeAWriterWithoutSelfDeadlock) {
    auto o = mem::own(Point{0, 0});
    { auto r = o.rread().acquire();  EXPECT_EQ(r.read().x, 0); }  // finish reading (release)
    { auto w = o.rwrite().acquire(); auto p = w.read(); p.x = 7; w.write(p); }  // then write — no wait-on-self
    { auto r = o.rread().acquire();  EXPECT_EQ(r.read().x, 7); }  // and read again (renew)
    SUCCEED();
}

// ── scheduling: priority is a compile-time argument on rwrite; higher is served first ────

namespace { enum class Job : std::uint8_t { normal = 0, high = 10 }; }   // arbitrary names; higher = higher priority

TEST(Memory, HigherPriorityWriteServedFirst) {
    auto o = mem::own(std::string(""));
    std::atomic<bool> blocker_holding{false}, release_blocker{false};
    // Hold a read lease so both writers must queue; enqueue the normal one first, then the high one.
    std::thread blocker([&] {
        auto r = o.rread().acquire();
        blocker_holding = true;
        while (!release_blocker) std::this_thread::yield();  // hold the read lease so both writers QUEUE
    });
    while (!blocker_holding) std::this_thread::yield();
    std::thread lo([&]{ auto w = o.rwrite<Job::normal>().acquire(); w.write(w.read() + "L"); });
    std::this_thread::sleep_for(2ms);                          // ensure L enqueues first
    std::thread hi([&]{ auto w = o.rwrite<Job::high>().acquire();   w.write(w.read() + "H"); });
    std::this_thread::sleep_for(2ms);
    release_blocker = true;                                    // now the queue drains by priority
    blocker.join(); lo.join(); hi.join();
    EXPECT_EQ(o.rread().acquire().read(), "HL");              // High ran before Low despite arriving later
}

// ── negative priority = immediate-write: bypass queue, preempt active writer, it resumes ──

TEST(Memory, ImmediateConstantIsANegativeNamedForReadability) {
    static_assert(mem::immediate == -1, "memory::immediate is the readable spelling of -1");
    static_assert(mem::immediate < 0,   "any negative priority is an immediate-write");
    SUCCEED();
}

// THE RULE THIS TEST OBEYS, and it is the whole reason it looks like this: a preempted writer may
// wait ONLY by polling `w.valid()`. That poll is what ACKS the preempt — `lease.hpp`: *"the first
// observation of a stop acks and wakes the owner … polling this from the holding thread is what lets
// a drain/preempt make progress"* — and `grant_immediate()` blocks on
// `writer_gate_->acked` until it happens. A writer that waits on anything else while holding its
// lease (a condition variable, a flag only the immediate-write can set) is a CIRCULAR WAIT: the
// immediate cannot proceed until the writer acks, and the writer will not ack until the immediate
// proceeds. That is a deadlock, not a flake, and it is how a previous attempt at this test ended.
//
// WHAT WAS WRONG BEFORE. The writer slept 1 ms after each of three chunks and the main thread slept
// 1 ms once, so the writer needed ~3 ms and the preempt was aimed at a 1 ms window. `sleep_for` was
// doing the job of a synchronisation primitive, and under load the main thread was descheduled past
// the writer entirely: the writer finished first, set `finished_first = 2`, and the assertion below
// failed. Roughly one run in three on a loaded machine, and it blocked every push.
//
// Both interleavings are LEGAL to the module — `grant_immediate()` short-circuits on `!writer_`
// with the comment *"a non-looping writer may release during the wait"* — so the module was never
// the bug. This test is about the preempting interleaving specifically, so it now ARRANGES that
// interleaving instead of gambling on the scheduler for it.
TEST(Memory, NegativePriorityImmediateWritePreemptsTheActiveWriterWhichThenResumes) {
    using clock = std::chrono::steady_clock;
    // Every wait below is bounded. An unbounded spin would turn a genuine preempt/resume regression
    // into a hung CI runner, which is a strictly worse failure than the flake being fixed here.
    constexpr auto kPatience = std::chrono::seconds(5);

    auto o = mem::own(std::string(""));
    std::atomic<bool> writer_holding{false};
    std::atomic<bool> preempt_seen{false};
    std::atomic<bool> timed_out{false};
    std::atomic<const char*> stuck_at{nullptr};
    std::atomic<int> chunks_at_stall{-1};
    std::atomic<int>  chunks{0}, finished_first{0};   // 1 = immediate finished first, 2 = writer

    // Cooperative spin: polls (so a preempt can make progress) and gives up rather than hanging.
    //
    // IT BACKS OFF, and that is not a nicety. A pure `yield()` loop keeps this thread permanently
    // runnable, and on a loaded box the scheduler will happily keep feeding it while starving the
    // very thread it is waiting for — the writer spinning at full tilt can hold off the main thread
    // that owes it the preempt. That showed up as this test's own 5 s deadline firing under a 12x
    // load. Spin briefly for latency, then sleep so somebody else can run.
    const auto spin_until = [&](const char* what, auto&& done) {
        const auto deadline = clock::now() + kPatience;
        for (int i = 0; !done(); ++i) {
            if (clock::now() > deadline) {
                stuck_at = what; chunks_at_stall = chunks.load(); timed_out = true; return false;
            }
            if (i < 1000) std::this_thread::yield();
            else          std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return true;
    };

    // A long-running writer: appends "A" three times, yielding to an immediate-write between chunks.
    std::thread writer([&] {
        auto w = o.rwrite().acquire();                        // priority 0
        writer_holding = true;
        // ANY observation of `!valid()` is a preempt, wherever it happens — and it must be recorded
        // there, not only at the top of the loop. Checking only at the top was a real bug and cost a
        // 5 s stall: the immediate-write can be absorbed ENTIRELY by the await-regrant spin below,
        // because that spin's `valid()` call is itself the ack. The writer would then resume with
        // `preempt_seen` still false and sit waiting for a second preempt nobody was going to send.
        const auto lease_valid = [&] {
            const bool v = w.valid();
            if (!v) preempt_seen = true;
            return v;
        };
        for (int i = 0; i < 3; ++i) {
            if (!spin_until("writer:await-regrant", lease_valid)) return;  // await regrant
            w.write(w.read() + "A");                          // safe: w.valid(), write() is the CURRENT location
            ++chunks;
            // Refuse to finish until the preemption has actually been OBSERVED. This is what makes
            // `finished_first` a fact rather than a coin flip. Skipped when the immediate-write
            // already came and went before the first chunk — waiting for a second preempt that
            // nobody will send would hang.
            if (i == 0 && !preempt_seen) {
                if (!spin_until("writer:await-preempt", [&] { return !lease_valid(); })) return;
                preempt_seen = true;
            }
        }
        if (finished_first == 0) finished_first = 2;
    });

    ASSERT_TRUE(spin_until("main:await-writer-holding", [&] { return writer_holding.load(); })) << "the writer never acquired";
    {
        auto w = o.rwrite<mem::immediate>().acquire();        // NEGATIVE priority (== -1) -> immediate-write
        w.write(w.read() + "!");                                     // emergency correction, mid-writer
        if (finished_first == 0) finished_first = 1;
    }                                                         // release -> writer's lease becomes valid() again
    writer.join();

    ASSERT_FALSE(timed_out) << "a wait exceeded " << kPatience.count() << "s at ["
                            << (stuck_at.load() ? stuck_at.load() : "?") << "] with chunks="
                            << chunks_at_stall.load() << " preempt_seen=" << preempt_seen.load()
                            << " finished_first=" << finished_first.load()
                            << " — the preempt/resume handshake is not completing";
    EXPECT_EQ(finished_first, 1);                            // the immediate-write completed before the writer resumed
    EXPECT_EQ(chunks, 3);                                    // the preempted writer resumed and finished all its work
    const auto result = o.rread().acquire().read();
    EXPECT_NE(result.find('!'), std::string::npos);         // the emergency write landed
    EXPECT_EQ(std::count(result.begin(), result.end(), 'A'), 3);  // the preempted writer's work survived
}

// ── compile-time-only write renewal (deliberate friction) ────────────────────────────────

TEST(Memory, WriteRenewalIsCompileTimeOnly) {
    // A plain write lease is one-shot; a renewable write lease is a DISTINCT, compile-time-selected
    // type. The type system — not a runtime flag — is what lets a writer re-lease.
    static_assert(!std::is_same_v<mem::Lease<int, mem::write>, mem::Lease<int, mem::write_renewable>>,
                  "renewable write is its own type, declared at compile time");
    SUCCEED();
}
