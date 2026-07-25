// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file owner.hpp
 * @brief `memory::Owner<T>` — the sole owner + scheduling coordinator — and the `own()` factory.
 *
 * `T` is the only class template argument; scheduling `policy` is a constructor value, a write's
 * priority is a compile-time argument on `rwrite`. Non-copyable and pinned (so `&value_` is stable
 * forever — a renewed reader never dangles even when the value's internal buffer reallocates).
 *
 * The coordinator is a hand-rolled priority reader/writer lock (one `std::mutex` + `condition_variable`
 * over explicit state — `std::shared_mutex` can't honor write priorities). Guarantees:
 *   - readers share; a writer is exclusive (no torn reads, no lost updates) — synchronized via the
 *     coordinator mutex at every grant/release, so object access is race-free with no lock held during use.
 *   - **drain-before-write**: a write request flips the current read generation's gate, so readers that
 *     loop on `valid()` yield; the write proceeds only once `readers_ == 0`.
 *   - **priority**: waiting writes are ordered by `(priority desc, arrival asc)` in a priority_queue.
 *   - **immediate-write** (`rwrite<memory::immediate>()`, i.e. priority < 0): bypasses the queue; if a
 *     writer is active AND cooperating (loops on `valid()`), it preempts that writer, does its write,
 *     then the writer resumes; against a non-looping writer it simply waits for it to finish, then goes
 *     ahead of the queue.
 */

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

#include "lease.hpp"
#include "mode.hpp"
#include "ownable.hpp"
#include "policy.hpp"
#include "request.hpp"

namespace cheatah::memory {

/**
 * @brief The sole owner + scheduling coordinator for one `T`. Non-copyable and pinned, so the object
 * never moves; every access goes through a request → acquire → lease. @tparam T the owned type.
 */
template <Ownable T>
class Owner {
public:
    /// Take sole ownership by MOVING @p value in — the object is consumed (its resources move into the
    /// Owner), never copied. An lvalue won't bind here (bind an rvalue: `own(std::move(x))`).
    /// @param value the object to take ownership of (moved in).
    /// @param pol the scheduling policy (interleave / writes_first).
    /// @complexity O(1) plus moving @p value.
    /// @alloc one read-generation gate (`std::make_shared`); the value's own storage just moves.
    /// @concurrency construct before sharing; the pinned Owner must outlive every lease and every
    /// thread that uses it.
    /// @test Memory.OwnerConsumesAndMovesTheObjectInNeverCopies
    explicit Owner(T&& value, policy pol = policy::interleave)
        : value_(std::move(value)), policy_(pol) { read_gate_ = make_gate(); }
    Owner(const Owner&) = delete;                // copying an owner is forbidden — there is one owner.
    Owner& operator=(const Owner&) = delete;     // sole ownership; pinned (mutex is non-movable).
    Owner(Owner&&) = delete;                      // pinned: &value_ must stay stable for live leases.
    Owner& operator=(Owner&&) = delete;

    /// Request a shared READ lease. Blocks — in THIS call: the grant is synchronous, so the returned
    /// request is already fulfilled — only while a write is pending/active or queued; otherwise many
    /// read leases coexist. @return a request for a read lease.
    /// @complexity O(1) amortized. @alloc the request's promise/future.
    /// @concurrency callable from any thread; readers share. Writer-preference: this waits while any
    /// write is active, suspended, or queued, so writers cannot starve.
    /// @warning requesting while the SAME thread still holds a lease on this owner can deadlock (a
    /// queued write makes the grant wait on that very lease) — renewal is release, then re-request.
    /// @test Memory.ReadLeasesCoexist
    /// @test MemoryConcurrency.ManyReadersCoexistThenAWriteDrains
    /// @systest MemoryCheatah.ReadLeaseValidState
    Request<Lease<T, read>> rread() {
        std::promise<Lease<T, read>> p;
        auto fut = p.get_future();
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [&] { return can_read(); });
            ++readers_;
            auto gate = read_gate_;
            p.set_value(Lease<T, read>(&value_, std::move(gate), [this] { release_read(); }));
        }
        return Request<Lease<T, read>>(std::move(fut));
    }

    /// Request an exclusive WRITE lease at compile-time `priority` (a plain int or the caller's enum;
    /// higher = served first, ties FIFO). `priority < 0` (spell it `memory::immediate`) is an
    /// immediate-write. Blocks in THIS call — the grant is synchronous, so the returned request is
    /// already fulfilled — until the readers drain and this write wins the queue.
    /// @tparam priority the compile-time write priority. @return a request for a write
    /// lease. @complexity O(log k) to enqueue among k waiters (O(1) immediate), plus the blocking wait.
    /// @alloc the request's promise/future, two fresh gates (`std::make_shared`: this write's own +
    /// the next read generation's), plus one queue-ticket slot (amortized) for a non-immediate write.
    /// @concurrency callable from any thread. Drain-before-write: flips the current read generation's
    /// gate and waits until every reader has released and no other write is active; an immediate-write
    /// skips the queue and additionally preempts a cooperating active writer (which resumes after).
    /// @warning requesting while the SAME thread still holds a lease on this owner deadlocks (the
    /// drain waits on that very lease) — release first, then re-request.
    /// @test Memory.WriteWaitsForReadersToDrain
    /// @test Memory.HigherPriorityWriteServedFirst
    /// @test Memory.NegativePriorityImmediateWritePreemptsTheActiveWriterWhichThenResumes
    /// @test MemoryConcurrency.ManyWritersDeterministicSum
    /// @systest MemoryCheatah.ConcurrentSumOverSharedOwner
    template <auto priority = 0>
    Request<Lease<T, write>> rwrite() {
        constexpr long long P = static_cast<long long>(priority);
        if constexpr (P < 0) return grant_immediate();
        else                 return grant_write(P);
    }

private:
    // ── the object + coordinator state (all guarded by mtx_) ──
    T                          value_;
    policy                     policy_;
    std::mutex                 mtx_;
    std::condition_variable    cv_;
    long long                  readers_ = 0;      ///< active read leases.
    bool                       writer_ = false;   ///< an active (non-suspended) write lease.
    bool                       writer_suspended_ = false;  ///< active writer paused for an immediate-write.
    bool                       immediate_ = false;///< an immediate-write holds exclusive access.
    std::shared_ptr<detail::Gate> read_gate_;     ///< current read generation's yield gate.
    std::shared_ptr<detail::Gate> writer_gate_;   ///< the active writer's gate (for preempt/resume).

    struct Ticket { long long prio; std::uint64_t seq; };
    struct ServedLater {  // priority_queue is a max-heap: top = highest priority, then earliest arrival.
        /**
         * The heap ordering: is @p a served later than @p b? Higher priority wins; within a
         * priority, the earlier arrival (lower seq) wins — FIFO among equals.
         * @param a one waiting write's ticket.
         * @param b the other waiting write's ticket.
         * @return true iff @p a is served after @p b.
         * @complexity O(1).
         * @alloc none.
         * @test Memory.HigherPriorityWriteServedFirst
         */
        bool operator()(const Ticket& a, const Ticket& b) const {
            return a.prio != b.prio ? a.prio < b.prio : a.seq > b.seq;
        }
    };
    std::priority_queue<Ticket, std::vector<Ticket>, ServedLater> wq_;  ///< waiting non-immediate writes.
    std::uint64_t seq_ = 0;

    // Readers proceed only when no writer/immediate is active or paused and no writer is queued
    // (writer-preference — this is what forces the drain and prevents writer starvation).
    bool can_read() const { return !writer_ && !immediate_ && !writer_suspended_ && wq_.empty(); }

    // A gate whose holder, on observing !valid, wakes our cv_. The wake must synchronize on
    // mtx_ before notifying: the holder acks from outside the lock, so a bare notify_all could
    // land while grant_immediate() still holds mtx_ evaluating its wait predicate (acked read
    // as false, waiter not yet blocked) — and since the ack is one-shot, that lost wakeup left
    // the preempting write asleep forever against a writer spinning on valid(). Taking and
    // releasing mtx_ first pins the wake after the waiter is actually waiting.
    std::shared_ptr<detail::Gate> make_gate() {
        auto g = std::make_shared<detail::Gate>();
        g->wake = [this] {
            { std::lock_guard<std::mutex> lk(mtx_); }  // serialize with a waiter mid-predicate
            cv_.notify_all();
        };
        return g;
    }

    Request<Lease<T, write>> grant_write(long long prio) {
        std::promise<Lease<T, write>> p;
        auto fut = p.get_future();
        {
            std::unique_lock<std::mutex> lk(mtx_);
            const std::uint64_t my = ++seq_;
            wq_.push({prio, my});
            read_gate_->valid.store(false);   // ask current readers to yield (drain)
            cv_.notify_all();
            cv_.wait(lk, [&] {
                return !writer_ && !immediate_ && !writer_suspended_ && readers_ == 0 &&
                       !wq_.empty() && wq_.top().seq == my;   // no active access + I'm the winner
            });
            wq_.pop();
            writer_ = true;
            writer_gate_ = make_gate();  // fresh, valid — this writer is preemptible
            read_gate_ = make_gate();     // fresh read generation for future readers
            p.set_value(Lease<T, write>(&value_, writer_gate_, [this] { release_write(); }));
        }
        return Request<Lease<T, write>>(std::move(fut));
    }

    Request<Lease<T, write>> grant_immediate() {
        std::promise<Lease<T, write>> p;
        auto fut = p.get_future();
        {
            std::unique_lock<std::mutex> lk(mtx_);
            if (writer_) {                                   // preempt a cooperating active writer
                writer_gate_->valid.store(false);
                cv_.notify_all();
                // Short-circuit !writer_ FIRST: a non-looping writer may release (writer_gate_ ->
                // nullptr) during the wait, so never deref the gate once the writer is gone.
                cv_.wait(lk, [&] { return !writer_ || writer_gate_->acked.load(); });
                if (writer_) { writer_suspended_ = true; writer_ = false; }  // it paused → suspend it
                // else it finished on its own; no resume owed.
            }
            read_gate_->valid.store(false);                  // drain any readers
            cv_.notify_all();
            cv_.wait(lk, [&] { return readers_ == 0 && !immediate_; });
            immediate_ = true;
            read_gate_ = make_gate();
            auto ig = make_gate();
            p.set_value(Lease<T, write>(&value_, std::move(ig), [this] { release_immediate(); }));
        }
        return Request<Lease<T, write>>(std::move(fut));
    }

    void release_read() {
        std::lock_guard<std::mutex> lk(mtx_);
        --readers_;
        cv_.notify_all();
    }
    void release_write() {
        std::lock_guard<std::mutex> lk(mtx_);
        writer_ = false;
        writer_gate_ = nullptr;
        cv_.notify_all();
    }
    void release_immediate() {
        std::lock_guard<std::mutex> lk(mtx_);
        immediate_ = false;
        if (writer_suspended_) {                     // resume the writer we preempted (before the queue)
            writer_suspended_ = false;
            writer_ = true;
            writer_gate_->acked.store(false);
            writer_gate_->valid.store(true);         // its valid() flips back to true → it continues
        }
        cv_.notify_all();
    }
};

/// Take sole ownership of @p value and hand back its `Owner`. @tparam T the owned type.
/// @param value the object to own (moved in). @param pol the scheduling policy.
/// @return an `Owner<T>` that has consumed @p value.
/// @complexity O(1) plus moving @p value. @alloc one read-generation gate (`std::make_shared`, in
/// the `Owner` constructor); the value's own storage just moves.
/// @test Memory.ObjectDiesWithOwner
template <Ownable T>
Owner<T> own(T value, policy pol = policy::interleave) { return Owner<T>(std::move(value), pol); }

}  // namespace cheatah::memory
