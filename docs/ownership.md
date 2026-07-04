# Ownership {#ownership}

<div class="cheetah-slogan">🐱 <em>one owner, many readers, no dangling — ever.</em> 🐆</div>

cheatah has **one** ownership story, and it fits on a page. A value lives in exactly
one place. If two parts of your program (or two threads) need the same value, you give
it a single **Owner** and everyone else **borrows** it for a moment at a time — through
a **lease** the owner grants and takes back. That's the whole model.

## Owning

```purr
import memory

let scores = memory.own([90, 84, 77])   # the list now has ONE owner
```

`memory.own(value)` **consumes** the value — it is moved in, never copied. The
`Owner` it returns is pinned (it never relocates) and non-copyable (there is never a
second owner). When the `Owner` goes out of scope, the value is destroyed. Nothing to
free, nothing to leak, no garbage collector.

## Borrowing: request → acquire → lease

You never touch the owned value directly. You *request* access, *acquire* a lease when
the owner grants it, and use the value only through that lease:

```purr
with scores.rread().acquire() as r {     # a shared READ lease — many can coexist
    io.print(r.read(0))                  # 90
}

with scores.rwrite().acquire() as w {    # an exclusive WRITE lease
    w.write(0, 95)                       # set one element (deduced setter)
    w.write(w.read() + [70])             # or read-modify-write the whole value
}                                        # released here — on every exit path
```

Reads are shared: any number of read leases coexist. A write is exclusive: the owner
first flips every outstanding read lease to `expired()`, the readers finish their
iteration and release (the **drain**), and only then does the write begin. A reader
that comes back afterwards gets a fresh lease at the value's *current* location — so a
stale pointer is not a bug you can write.

`read()` gives you the value; `write(...)` is always a **setter**. There is no
operation that hands back a raw mutable reference, which is precisely why a borrow can
never outlive its grant.

## Why this matters for threads

`thread.spawn` copies every plain argument — but an `Owner` travels **by reference**,
and its leases are the one safe gate for shared mutable state. Two workers bumping the
same counter 50,000 times each through write leases yield **exactly** 100,000: the
drain-before-write engine makes the result deterministic under any interleaving. An
`Owner` *is* the lock — `memory.own(false)` is a stop latch, `memory.own(list)` is a
queue — so there is no mutex/channel/event vocabulary to learn. The full contract is on
the [thread](../stdlib/thread/README.md) and [memory](../stdlib/memory/README.md)
module pages.

## Urgency, when you need it

Queued writes are served highest-priority first (`o.rwrite<10>()` beats `o.rwrite()`),
and `o.rwrite<memory.immediate>()` can *briefly preempt* a cooperating writer for one
emergency write — the paused writer then resumes exactly where it was, its lease valid
again. Bounded, visible, and the only queue-bypass in the module.

## The C++ underneath

The engine is deliberately small, readable, and dependency-free — one mutex and
condition variable over explicit state, a priority queue of waiting writes, and atomic
gates for the yield handshake. If you want to see exactly what runs:

- [stdlib/memory/owner.hpp](../stdlib/memory/owner.hpp) — the `Owner` and its
  drain / priority / preempt-resume engine
- [stdlib/memory/lease.hpp](../stdlib/memory/lease.hpp) — the `Lease` handle and the
  owner↔holder gate
- [stdlib/memory/request.hpp](../stdlib/memory/request.hpp) — the request → acquire
  handshake
- [stdlib/memory/mode.hpp](../stdlib/memory/mode.hpp) and
  [stdlib/memory/policy.hpp](../stdlib/memory/policy.hpp) — the access-mode tags and
  scheduling policies

Behaviour is pinned by
[stdlib/memory/tests/memory_test.cpp](../stdlib/memory/tests/memory_test.cpp) and
[stdlib/memory/tests/memory_concurrency_test.cpp](../stdlib/memory/tests/memory_concurrency_test.cpp),
run under ASan/UBSan, ThreadSanitizer, and Valgrind on every QA-gate push.
