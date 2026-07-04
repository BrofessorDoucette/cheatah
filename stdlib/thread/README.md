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

## The one-sentence contract

**cheatah does not detect or prevent data races.** What you do across threads is at
your own risk; the language keeps single-thread guarantees intact and gives you exactly
one blessed gate for shared mutable state — a `memory.Owner<T>` and its leases.

## What is guaranteed

- **Every value is safe per-thread.** cheatah values (ints, floats, strings, lists,
  dicts, structs, ndarrays) have no hidden shared state — two threads each working on
  their *own* values never interfere.
- **`spawn` copies every copyable argument.** The worker owns its arguments; nothing in
  a spawned thread points back at the caller's locals. You cannot *accidentally* share
  a plain value across threads — passing it hands the worker a copy.
- **Every thread joins before `main` returns.** A `Thread` guard joins on destruction
  (drop it, `with` it, or unwind past it), and there is **no `detach`**: the cheatah
  host unloads the program's module right after `main`, so a detached thread would
  crash in unloaded code — and an unjoined thread would break the deterministic-cleanup
  guarantee that everything a program creates is released when it ends.
- **A worker exception cannot kill the program silently.** A `raise` escaping a worker
  is caught in that thread and rethrown at `t.join()` — handle it with `try`/`except`.
  If nobody joins, the guard's destructor reports it on stderr.

## What is your responsibility

Sharing one object across threads is undefined behavior **unless** that object is built
for it. The rules:

- **Share through a `memory.Owner<T>`.** An `Owner` is pinned and non-copyable, so
  `spawn` passes it **by reference** — the one deliberate exception to copy-in. Declare
  the `Owner` *before* the threads that use it (join-on-destroy then guarantees it
  outlives them), spell the worker's parameter with its type
  (`o : memory.Owner<int>`), and touch the value only through
  `o.rread()`/`o.rwrite()` leases. An `Owner` **is** the lock: `memory.own(false)`
  is a stop latch, `memory.own(list)` is a queue — there is no second synchronization
  vocabulary to learn. How the leases work is on the [memory](../memory/README.md)
  module page.
- **stdout interleaves.** `io.print` from two threads at once is safe (no corruption)
  but the lines can interleave. Funnel output through one thread, or print only after
  joining the workers.
- **`random` is per-thread.** Each thread has its own engine, self-seeded on first use;
  `random.seed(s)` seeds the *calling* thread only. Concurrent draws never race — but a
  worker that wants a reproducible stream must seed itself.
- **Set environment variables before spawning.** `os.setenv` mutates process-global
  state that is not thread-safe against concurrent `getenv` on most platforms.
- **A `cpp { }` block is outside all of this.** Raw C++ can share anything; nothing
  here (or anywhere) protects it.

## The shapes that work

One worker per source, retry on failure, deterministic shutdown — all in cheatah:

```purr
import io
import thread

fn feed(venue : str, product : str) {
    # ... connect, stream, enqueue ... (raises on a dropped feed)
}

fn supervise(venue : str, product : str) {
    let up = false
    while not up {
        let w = thread.spawn(feed, venue, product)
        try {
            w.join()
            up = true              # returned cleanly (stop requested)
        } except e {
            io.print("retrying", venue, "after:", e)
        }
    }
}

fn main() {
    with thread.spawn(supervise, "alpha", "X-Y") {
        with thread.spawn(supervise, "beta", "X-Y") {
            # main thread: the flush/coordination loop
        }
    }   # every worker joined here, on every exit path
}
```

Shared totals through an `Owner` — the `memory` module's request → acquire → lease flow:

```purr
import io
import memory
import thread

fn worker(o : memory.Owner<int>, quota : int) {
    for k in range(0, quota) {
        with o.rwrite().acquire() as w {
            w.write(w.read() + 1)
        }
    }
}

fn main() {
    let o = memory.own(0)
    with thread.spawn(worker, o, 50000) {
        with thread.spawn(worker, o, 50000) { }
    }
    io.print(o.rread().acquire().read())   # exactly 100000 — leases don't lose updates
}
```

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

## Under the hood

`thread` is a thin, honest layer: `std::jthread` + one shared error slot per spawn.
The QA gate runs the concurrency suites under **ThreadSanitizer** (the `tsan` preset)
in addition to ASan/UBSan and Valgrind — a data race in the standard library is a gate
failure, not a shrug. Your own `.purr` races are still yours: TSan guards the library's
promises, not your program's schedule.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[thread.hpp](thread.hpp). Tested in
[../tests/thread_test.cpp](../tests/thread_test.cpp) plus the compile-run and
system e2e suites (`tests/purrc/thread_cr_test.cpp`, `tests/purrc/thread_sys_test.cpp`);
ASan + **TSan** + Valgrind clean via the QA gate.
