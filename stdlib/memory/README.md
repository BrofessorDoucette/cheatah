# cheatah `memory`

One object, one **Owner**, and leases. `memory.own(value)` takes *sole* ownership of a
value by **moving it in** (it is consumed, never copied); the returned `Owner<T>` is
pinned and non-copyable, so its object never moves under you and a borrow can never
dangle. Every access is a **request → acquire → lease**:

```purr
import io
import memory

fn main() {
    let o = memory.own("hello")                 # Owner<str> — sole, pinned ownership

    with o.rwrite().acquire() as w {            # request a write -> block -> exclusive lease
        w.write(w.read() + " world")            # read-modify-write through the lease
    }                                           # lease released at scope exit

    with o.rread().acquire() as r {             # shared read lease — many coexist
        io.print(r.read())                      # hello world
    }
}
```

There is deliberately **no** way to get a bare reference out: the lease is the only
handle, `read()` returns the value and `write(...)` is a **setter** — so a raw handle
that outlives its grant is unrepresentable. For fine-grained updates a write lease has
deduced element setters: `w.write(index, v)` for sequences (`list`, `str`, `ndarray`),
`w.write(key, v)` for dicts.

## Reads coexist; a write drains them first

Any number of read leases are served at once. A write request flips every outstanding
read lease to `expired()` (readers poll `valid()` and release), waits for the **drain**
to finish, then writes — possibly relocating the object. A reader that re-requests
after the write gets a lease at the object's *current* location, so a renewed reader
never holds a stale pointer. The requesting thread's own read lease expires too, so
"upgrade my read to a write" can never self-deadlock.

## Priorities, policies, and the immediate-write

- `o.rwrite()` is priority `0`; `o.rwrite<10>()` jumps ahead of it (higher = served
  first, ties FIFO). Spell levels with your own enum for readability.
- The **policy** is fixed when ownership starts: `memory.own(v)` interleaves readers
  fairly between writes; `memory.own(v, memory.writes_first)` drains the whole write
  queue first (deliberate, visible starvation — a choice, never a safety hole).
- `o.rwrite<memory.immediate>()` (any negative priority) is the one queue-bypass: it
  **preempts a cooperating active writer** — that writer's lease reads `valid() ==
  false`, it pauses, the immediate write runs, and the paused writer **resumes
  transparently** with its lease valid again, pointed at the object's current location.
  Bounded by construction: one borrowed write, handed straight back.

```purr
fn writer(o : memory.Owner<str>) {
    with o.rwrite().acquire() as w {
        for i in range(0, 3) {
            while not w.valid() { }             # paused by an immediate-write: await regrant
            w.write(w.read() + "A")             # always the object's CURRENT location
        }
    }
}
# elsewhere, mid-writer:  with o.rwrite<memory.immediate>().acquire() as w { w.write(w.read() + "!") }
```

`.acquire(on_interrupt)` optionally carries a callback that fires (once, in the
holder's thread) when the owner asks the lease to yield — flip an atomic, notify your
loop; polling `valid()`/`expired()` works without it.

## Sharing across threads

The `Owner` is the [thread](../thread/README.md) module's one blessed gate for shared
mutable state: `thread.spawn` copies every copyable argument, but a pinned,
non-copyable `Owner` travels **by reference** — workers funnel every access through
leases, and the drain-before-write engine makes results exact under any interleaving.
`memory.own(false)` is a stop latch; `memory.own(list)` is a queue. There is no
mutex/channel/event vocabulary to learn.

## Renewing writers declare themselves

A write lease is one-shot. A writer that intends to write *repeatedly* must say so in
its type — `write_renewable`, a distinct compile-time lease — which makes "I keep
writing this shared thing" legible at the call site instead of a runtime flag.

## Under the hood

A hand-rolled priority reader/writer engine — one `std::mutex` + condition variable
over explicit state, a `std::priority_queue` of waiting writes, and per-generation
atomic gates for the yield/ack handshake (`std::shared_mutex` cannot honor write
priorities, so we do not use it). Requests wrap a `std::future` the language never
sees: cheatah's whole vocabulary is `own`, `rread`/`rwrite`, `acquire`, `read`/`write`,
`valid`/`expired`. The implementation is small and readable:
[owner.hpp](owner.hpp) (the engine), [lease.hpp](lease.hpp) (the handle + gate),
[request.hpp](request.hpp) (the handshake), [mode.hpp](mode.hpp) and
[policy.hpp](policy.hpp) (the tags). Design rationale lives in the repository's
`stdlib/memory/DESIGN.md`; the behaviour is pinned by
[tests/memory_test.cpp](tests/memory_test.cpp) and
[tests/memory_concurrency_test.cpp](tests/memory_concurrency_test.cpp), run under
ASan/UBSan, **ThreadSanitizer**, and Valgrind on every QA-gate push.
