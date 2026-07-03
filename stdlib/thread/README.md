# cheatah `thread`

Run a cheatah `fn` on another OS thread. The whole module is one factory and one
handle: `thread.spawn(f, args...)` starts `f(args...)` on a new thread and returns a
`Thread` — a move-only owning guard that **joins at scope exit**, so every thread
finishes before `main` returns. There is deliberately no `detach`.

```purr
import io
import thread

fn collect(venue : str, product : str, depth : int, funding : bool) {
    io.print(venue, product, depth, funding)
}

fn main() {
    let a = thread.spawn(collect, "alpha", "X-Y", 10, false)   # args are COPIED in
    a.join()                                                   # blocks; rethrows a worker raise
    with thread.spawn(collect, "beta", "Y-Z", 20, true) {
        # ... main-thread work while the worker runs ...
    }                                                          # joined here, every exit path
}
```

## Sharing state — the `memory` module's job

Every **copyable** argument is copied into the thread: the worker owns its values, and
nothing points back at the caller. The one way to share a mutable object is to pass a
pinned `memory.Owner<T>` — it travels **by reference** — and go through its
request → acquire → lease flow:

```purr
import memory
import thread

fn worker(o : memory.Owner<int>, quota : int) {   # an Owner parameter is spelled with its type
    # ... o.rwrite().acquire() / o.rread().acquire() ...
}

fn main() {
    let o = memory.own(0)
    let t = thread.spawn(worker, o, 50000)   # o by reference; it outlives t (declared first)
    t.join()
}
```

cheatah does **not** detect or prevent data races — what you do across threads is at
your own risk — but the two-module design funnels shared mutable state through the one
safe gate: an `Owner`'s leases. The full contract (what is and isn't safe to share,
stdout interleaving, per-thread `random`, `os.setenv`) is in
[docs/threading.md](../../docs/threading.md).

## Surface

- `spawn(f, args...)` — start `f(args...)` on a new thread; returns the owning `Thread`.
  Copyable args are copied in; a movable rvalue (a guard from a factory) is moved in; a
  non-copyable lvalue (an `Owner`) goes by reference. A non-copyable **temporary** does
  not compile (it would dangle — bind it to a variable first).
- `Thread.join()` — block until the worker finishes; **rethrows** the worker's escaped
  `raise`/exception (catch it with `try`/`except`). One-shot: a second join raises.
- `Thread.joinable()` — does this handle still own an unjoined thread?
- The destructor joins; a worker exception nobody joined for is reported on stderr
  (`cheatah thread: unhandled exception in thread: ...`).

Deliberately **not** included: `detach` (the host unloads the program's module right
after `main` — a detached thread would crash in unloaded code, and an unjoined thread
would break the deterministic-cleanup guarantee), mutexes/channels/events/atomics (an
`Owner` **is** the lock — `memory.own(false)` is a stop latch, `memory.own(list)` is a
queue), `sleep` (`time.sleep`), and `cpu_count` (`os.cpu_count`).

Per-function docs (parameters, runtime complexity, heap behavior) are in
[thread.hpp](thread.hpp). Tested in
[../tests/thread_test.cpp](../tests/thread_test.cpp) plus the compile-run and
system e2e suites (`tests/purrc/thread_cr_test.cpp`, `tests/purrc/thread_sys_test.cpp`);
ASan + **TSan** + Valgrind clean via the QA gate.
