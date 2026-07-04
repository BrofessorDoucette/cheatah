// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once
// cheatah-link: -pthread

/**
 * @file thread.hpp
 * @brief cheatah `thread` — run a cheatah `fn` on another OS thread. `import thread` to use it.
 *
 * The whole module is one factory and one handle: `thread.spawn(f, args...)` starts `f(args...)`
 * on a new thread and returns a `thread::Thread`, a move-only owning guard that JOINS at scope
 * exit — every thread is joined before `main` returns. There is deliberately no `detach`: the
 * cheatah host unloads the program's module right after `main`, so a detached thread would crash
 * in unloaded code, and an unjoined thread would break the deterministic-cleanup guarantee.
 *
 * Sharing state between threads is the `memory` module's job, not this one's. Every COPYABLE
 * argument is copied into the thread (the worker owns its own value; nothing points back at the
 * caller), so the one way to share a mutable object is to pass a pinned `memory.Owner<T>` — it
 * travels BY REFERENCE — and go through its request -> acquire -> lease flow. cheatah does not
 * detect or prevent data races: what you do across threads is AT YOUR OWN RISK, and the
 * `Owner`'s leases are the recommended safe path. The full contract lives on the module page (stdlib/thread/README.md).
 *
 * A worker that throws does not kill the program: the exception is caught in the thread and
 * RE-THROWN at `t.join()` (catch it there with `try`). If the thread is never explicitly joined,
 * the guard's destructor joins and reports the stored error on stderr instead (a destructor must
 * not throw).
 *
 * `import thread` includes this header AND links `libcheatah_thread`. Unit tests:
 * `stdlib/tests/thread_test.cpp`; the suite runs under AddressSanitizer + ThreadSanitizer and
 * Valgrind on every QA-gate run.
 */
#include <concepts>
#include <exception>
#include <memory>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cheatah::thread {

namespace detail {

/// Shared between the owning guard and the worker trampoline: the worker's escaped exception (if
/// any) and whether `join()` already surfaced it (so the destructor stays silent). Written by the
/// worker before it finishes and read only after a join — the join is the synchronization.
struct State {
    std::exception_ptr error;  ///< set by the trampoline if the worker threw.
    bool observed = false;     ///< `join()` rethrew it; the destructor must not re-report.
};

/// How `spawn` carries one argument into the thread: a copyable argument is DECAY-COPIED (the
/// thread owns its own value), a non-copyable RVALUE that can move (a guard returned by a
/// factory) is MOVED in, and a non-copyable, non-movable LVALUE (a pinned `memory::Owner`)
/// travels by address — the caller's object itself, which must outlive the thread.
template <class A>
inline constexpr bool holds_by_value =
    std::copy_constructible<std::remove_cvref_t<A>> ||
    (!std::is_lvalue_reference_v<A> && std::move_constructible<std::remove_cvref_t<A>>);

}  // namespace detail

/**
 * One argument `spawn` can carry into a thread: anything copyable, a movable rvalue, or a
 * non-copyable LVALUE (passed by reference — the caller's object must outlive the thread, which
 * the guard's join-on-destroy gives naturally when it is declared after the object).
 *
 * NOT satisfied by a non-copyable, non-movable TEMPORARY — e.g. `thread.spawn(f, memory.own(0))`
 * does not compile: the temporary Owner would die before the thread ran. Bind it to a variable
 * first (`let o = memory.own(0)`), then pass `o`.
 */
template <class A>
concept SpawnArg = detail::holds_by_value<A> || std::is_lvalue_reference_v<A>;

/**
 * The callables `spawn` accepts: invocable with the spawned copies/references of the given
 * arguments (each argument reaches the worker as an lvalue — the thread's own copy, or the
 * caller's non-copyable object by reference). Satisfied by both lowerings purrc emits for a
 * cheatah `fn` passed by name (the concrete function pointer and the generic forwarding lambda).
 */
template <class F, class... Args>
concept SpawnCallable = std::invocable<std::decay_t<F>&, std::remove_cvref_t<Args>&...>;

namespace detail {

/// What the trampoline's closure stores for one argument: the decayed value, or a pointer to the
/// caller's non-copyable object.
template <SpawnArg A>
using held_t = std::conditional_t<holds_by_value<A>, std::remove_cvref_t<A>,
                                  std::remove_cvref_t<A>*>;

/// Capture one argument for the thread (copy / move / take the lvalue's address — see
/// `holds_by_value`). @complexity O(1) plus the copy/move itself. @alloc whatever the copy makes.
template <SpawnArg A>
held_t<A> hold(A&& a) {
    if constexpr (holds_by_value<A>) {
        return std::forward<A>(a);
    } else {
        return std::addressof(a);
    }
}

/// Hand a held argument to the worker as an lvalue reference (the thread's own copy, or the
/// caller's object). @complexity O(1). @alloc none.
template <SpawnArg A>
std::remove_cvref_t<A>& unhold(held_t<A>& h) {
    if constexpr (holds_by_value<A>) {
        return h;
    } else {
        return *h;
    }
}

}  // namespace detail

/**
 * The owning handle to one spawned thread — obtained from `thread.spawn`, never constructed
 * directly by a cheatah program. Move-only (there is exactly one owner of a thread), and the
 * destructor JOINS: dropping the handle — normally, via `with`, or during unwinding — always
 * waits for the worker to finish, on every exit path. There is no `detach`.
 *
 * If the worker threw and `join()` never surfaced it, the destructor reports one line on stderr
 * (`cheatah thread: unhandled exception in thread: ...`) — the honest fallback, since a
 * destructor must not throw.
 *
 * @test CheatahThread.MoveTransfersOwnership
 * @test CheatahThread.DestructorJoinsARunningThread
 * @crtest ThreadCompileRun.SpawnJoin
 * @systest StdlibE2E.Thread
 */
class Thread {
public:
    /**
     * The grant path used by `spawn`: adopt a running thread and its shared error slot. Public
     * but not part of the cheatah surface (no module factory returns the pieces), matching the
     * memory module's no-`friend` stance.
     * @param t the running jthread to own.
     * @param state the error slot the spawn trampoline writes into.
     * @complexity O(1). @alloc none (moves the handles in).
     * @test CheatahThread.SpawnRunsTheWorker
     */
    Thread(std::jthread t, std::shared_ptr<detail::State> state) noexcept;

    /// Threads have exactly one owner: moving transfers it, the source becomes non-joinable.
    /// @param other the handle to take the thread from (left non-joinable).
    /// @complexity O(1). @alloc none. @test CheatahThread.MoveTransfersOwnership
    Thread(Thread&& other) noexcept;

    /// Move-assign: the destination first settles its own thread (join + report an unobserved
    /// error), then adopts the source's.
    /// @param other the handle to take the thread from (left non-joinable).
    /// @return this handle, now owning @p other's thread.
    /// @complexity O(join). @alloc none.
    /// @test CheatahThread.MoveAssignSettlesTheOldThread
    Thread& operator=(Thread&& other) noexcept;

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    /// Joins if still joinable; reports an unobserved worker exception on stderr (one line).
    /// @complexity O(join) — blocks until the worker finishes. @alloc none.
    /// @test CheatahThread.DestructorJoinsARunningThread
    /// @test CheatahThread.DestructorReportsAnUnobservedException
    ~Thread();

    /**
     * Block until the worker finishes. If the worker escaped with an exception, RE-THROW it here
     * — catch it with `try { t.join() } catch e { ... }`. Joining a thread that was already
     * joined (or moved away) raises.
     * @complexity O(join) — blocks until the worker finishes. @alloc none.
     * @test CheatahThread.JoinRethrowsTheWorkersException
     * @crtest ThreadCompileRun.JoinCatchesWorkerRaise
     * @systest StdlibE2E.Thread
     */
    void join();

    /**
     * Does this handle still own a running/unjoined thread?
     * @return true until `join()` (or a move-away); false after.
     * @complexity O(1). @alloc none.
     * @test CheatahThread.JoinableLifecycle
     * @crtest ThreadCompileRun.Joinable
     */
    bool joinable() const noexcept;

private:
    void settle() noexcept;  // join if joinable; stderr-report an unobserved worker error.

    std::jthread t_;
    std::shared_ptr<detail::State> state_;
};

/**
 * Start `f(args...)` on a new OS thread and return its owning `Thread` guard.
 *
 * `f` is a cheatah `fn` passed by name (or any C++ callable). Every copyable argument is COPIED
 * into the thread — the worker owns its values, nothing refers back to the caller — so plain
 * ints/floats/strings/lists/structs are always safe to pass. A non-copyable, pinned object (a
 * `memory.Owner<T>`) is passed BY REFERENCE: it must outlive the thread, which the guard's
 * join-on-destroy gives naturally when the `Thread` is declared after the `Owner`. The worker
 * declares such a parameter with its full type (`o : memory.Owner<int>`).
 *
 * The worker runs to completion exactly once; an exception it escapes with is caught and
 * re-thrown at `join()`. Sharing mutable state across threads is safe ONLY through a
 * `memory.Owner`'s leases — anything else you share is at your own risk.
 *
 * @param f the cheatah `fn` (or callable) to run.
 * @param args its arguments (copied in; a non-copyable lvalue by reference — see above).
 * @return the owning `Thread` guard (joins at scope exit).
 * @complexity O(1) plus copying the arguments and the OS thread start.
 * @alloc one shared error slot + the thread's argument copies + the OS thread stack.
 * @test CheatahThread.SpawnRunsTheWorker
 * @test CheatahThread.SpawnPassesANonCopyableByReference
 * @crtest ThreadCompileRun.SpawnJoin
 * @systest StdlibE2E.Thread
 */
template <class F, class... Args>
    requires SpawnCallable<F, Args...> && (SpawnArg<Args> && ...)
Thread spawn(F f, Args&&... args) {
    auto state = std::make_shared<detail::State>();
    std::jthread t(
        [state, fn = std::move(f),
         held = std::tuple<detail::held_t<Args>...>(
             detail::hold<Args>(std::forward<Args>(args))...)]() mutable {
            try {
                [&]<std::size_t... I>(std::index_sequence<I...>) {
                    fn(detail::unhold<Args>(std::get<I>(held))...);
                }(std::index_sequence_for<Args...>{});
            } catch (...) {
                state->error = std::current_exception();
            }
        });
    return Thread(std::move(t), std::move(state));
}

}  // namespace cheatah::thread
