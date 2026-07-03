# `memory` — design & implementation notes (Owner / Lease / Request)

> **Status: implemented and shipping.** The Owner scheduling engine is built (`owner.hpp`) and green:
> the C++ suites (`tests/memory_test.cpp`, `tests/memory_concurrency_test.cpp`) and the cheatah `.purr`
> surface tests all pass in the gate, ASan/UBSan-clean. This document is the design rationale; the
> API below matches the code (setter `write(...)` / symmetric `read(...)` getters, `Owner(T&&)`
> move-in, concept-gated container access).

A **C++ backend** that cheatah `import memory`s. It gives ownership and access two names and
enforces memory safety by construction. It is a **thin layer over bare standard-library primitives** —
a shared mutex, stop tokens, and, for the request handshake, `std::promise` / `std::future`. What you
get back from an accessor is a `memory::Request<Lease>`, a small **move-only wrapper around a
`std::future`** (composition — no inheritance, no `friend`); you block for the lease with
`.acquire(on_interrupt)`, so cheatah never has to learn the words "future"/"promise" yet. The safety
comes from *what* we hand back (a lease you can only read/write through), not from hiding the plumbing.

**No garbage collection. No `view` type. No copy/clone. No heap of ours.** Just modern-C++
`std::shared_mutex` / `std::stop_source` / `std::stop_token` / `std::promise` / `std::future` /
`std::priority_queue`, wrapped into `Lease`, `Request`, and `Owner` (plus a free `own()` factory).

The module is split into focused headers under a `memory.hpp` umbrella: `mode.hpp` (access-mode tags +
the `Mode` concept), `policy.hpp` (the `policy` enum + `immediate`), `lease.hpp` (`Lease<T, Mode>`),
`request.hpp` (`Request<Lease>`), `owner.hpp` (`Owner<T>` + `own()`).

## The two types

### `Owner<T>` — sole owner + coordinator
**`T` is the only template argument, ever.** The class is *not* parameterised on policy or priority:
- the **scheduling policy** is a value passed **at construction** (and stored), and
- the **priority** of a write is a value passed **at the call**, as a template argument on the
  accessor (`o.rwrite<priority>()`).

Crucially, **every accessor is a *request* — it returns a `memory::Request<Lease<…>>`, never a lease
directly** (a read included). The owner may not be ready to hand out access the instant you ask
(a write may be pending, a drain in progress); a request lets the owner decide *when* to grant.
There is deliberately **no** API that returns a bare lease — so "I got handed access I wasn't really
cleared for" is unrepresentable. A `Request<L>` wraps a `std::future<L>`; you redeem it with
`.acquire(on_interrupt)` (blocks until the owner fulfills the matching internal `std::promise`).

```cpp
template <std::movable T>
class Owner {
    explicit Owner(T value, memory::policy pol = memory::interleave);  // policy is fixed at construction
    Owner(const Owner&) = delete;                                      // sole ownership; pinned

    // request-read: a request for a shared read lease, fulfilled when the owner is ready.
    memory::Request<Lease<T, memory::read>> rread();

    // request-write: a request for an exclusive write lease. `priority` is a compile-time value — a
    // plain int, or a value of the caller's own enum. Higher = higher priority; ties break by arrival
    // (FIFO). A NEGATIVE priority is an immediate-write (see "Negative priority = immediate-write").
    template <auto priority = 0>
    memory::Request<Lease<T, memory::write>> rwrite();
};
```
- Holds the value and coordinates all access. **Non-copyable, pinned** — there is never a second
  owner, so when the owner drops, the object is destroyed.
- **`memory::policy`** (a plain runtime enum, given to the constructor) chooses how pending writes are
  scheduled against reads:
  - **`memory::interleave`** (default) — readers renew *between* writes; writers and waiting readers are
    served fairly.
  - **`memory::writes_first`** — drain the **entire** write queue before renewing any reader. (Under a
    steady write stream this starves readers — but the owner *chose* that policy at construction; it
    is a deliberate, visible declaration, never a memory-safety hole.)
- **Priority is a compile-time argument on `rwrite`.** `o.rwrite()` is priority `0`; `o.rwrite<10>()`
  jumps ahead of it; the caller may use its own enum for legible names —
  `enum class Job { normal = 0, ui = 10, alarm = 100 };` then `o.rwrite<Job::alarm>()`.
  The only rule the value must obey: **higher = higher priority**, and **negative = immediate**. The
  queue orders by `(static_cast<long long>(priority) desc, arrival asc)`.
- **`memory::immediate`** — a named constant, `= -1`, the canonical spelling of an immediate-write:
  `o.rwrite<memory::immediate>()` reads far better than `o.rwrite<-1>()`. (Any negative value is an
  immediate-write; `memory::immediate` is simply the readable name for it.)

### The request handshake — `memory::Request<Lease>` and `.acquire(on_interrupt)`
`rread()` / `rwrite<…>()` return a `memory::Request<Lease<…>>` — a small move-only wrapper over a
`std::future<Lease<…>>` (the future/promise stay inside; cheatah never learns those words). It is
**non-blocking to obtain**: you hold "a request for a lease." You then **block** for the lease with a
single method:
```cpp
template <class LeaseT>
class Request {
    std::future<LeaseT> fut_;                      // owner-fulfilled; never exposed
public:
    explicit Request(std::future<LeaseT>&& f);
    Request(Request&&) = default;                  // move-only, like the future it wraps

    // BLOCK until the owner grants, then return the lease. `on_interrupt` is the requester's
    // interruption handler: it is wired (via Lease::on_interrupt, public — no friend) to the granted
    // lease's stop token, so if the owner later needs the lease back (a writer waiting on a read; an
    // immediate-write on a write) the handler fires. Optional — omit it to poll valid()/expired().
    LeaseT acquire(std::function<void()> on_interrupt = {});
};
```
The three-step flow reads as ownership → a request → the acquired lease:
```cpp
auto x = own(value);                    // Owner<T>
auto r = x.rread();                     // memory::Request<Lease<T, read>>   (no block yet)
auto l = r.acquire([]{ /* asked to yield: signal my loop */ });   // BLOCKS -> Lease<T, read>
l.read();                               // now read (see below)
```
A request is one-shot (acquire once); each `rread()`/`rwrite<…>()` returns a fresh one. **Renewal
reuses exactly this mechanism** — the owner's renewal promise resolves to a fresh lease at the object's
current location, so a renewed reader is just another `request.acquire(…)`.

**Why the callback rides with the request.** Passing `on_interrupt` *into* `acquire` is what lets the
owner truly own: the owner decides *when* to interrupt, and the requester only gets to *politely
suggest what to do* when it happens. Mechanically, `acquire` blocks on the future, then registers
`on_interrupt` as a `std::stop_callback` on the lease's `std::stop_token`, held for the lease's
lifetime (kept behind a `std::unique_ptr` so the lease stays movable — `std::stop_callback` itself is
non-movable). The handler fires in the interrupting thread, so keep it light (flip an atomic, notify a
condition) and let your own loop react on its next `valid()` check.

### `Lease<T, memory::read | memory::write | memory::write_renewable>` — the only handle to the object
- The one way to touch the object. Holds its grant for its lifetime plus a direct pointer to the
  object. RAII / move-only.
- **Access is `read()` / `write(value)`, never `get()`.** A read lease is read with `r.read()`
  (→ `const T&`); a write lease is **set with `w.write(value)`** (the obvious setter — not
  `w.write() = value`). For in-place mutation of a container/struct where copying the whole object to
  set it would be wasteful, the write lease also offers a no-arg `w.write()` → `T&`
  (`w.write()[i] = x`, `w.write().field = x`, `w.write().push_back(…)`). These are the **customization
  seam** — a user can later define a lease type that decorates access without the owner or caller
  changing. (Whether that dispatch is virtual or CRTP is deferred — see Open/deferred.)
- **read lease** (`memory::read`, `std::shared_lock`): `r.read()` → `const T&`. Many coexist. Carries a
  `std::stop_token`, so the reader can tell when the owner needs it to stop:
  - `r.valid()`  == `!stop_requested()` — still ours to read.
  - `r.expired()` == `stop_requested()` — a writer is waiting; finish this iteration and release.
- **write lease** (`memory::write`, `std::unique_lock`): `w.write(value)` sets it; `w.write()` → `T&`
  for in-place mutation; `w.read()` → `const T&` (a writer often reads-modify-writes). Exclusive. It
  **also** carries `valid()`/`expired()` — but
  a *normal* write is only ever tripped by an **immediate-write**. A long-running writer that wants to
  be preemptible loops `while (w.valid())` just like a reader; a writer that never checks simply runs
  its lease to completion.

## Lifecycle — safe by *drain-before-write*

1. **Reads are cheap when uncontended.** A `rread()` whose grant isn't blocked by a pending write is
   fulfilled at once — the returned request's `.acquire()` returns straight away with a shared-lock read
   lease, and any number coexist. The request/promise shape is uniform, but the fast path builds no wait.
2. **A write request expires all reads.** `o.rwrite<…>()` requests a stop on the current
   read-generation's single `std::stop_source`; **every** outstanding read lease (including one held
   by the *same* thread now asking to write) flips to `expired()`. Each reader decides, right then,
   **renew-or-not**.
3. **The writer waits for a clean drain.** The write begins only **after every read lease has actually
   released**. Because the requester's own read lease expired too, it is *not* waiting on itself —
   only on the *other* readers. No self-deadlock; a reader "upgrading" to writer is just expire-all →
   others drain → I write → we all re-read.
4. **The writer writes**, possibly **moving** the object.
5. **The owner fulfills promises.** The write lease ends; the owner (per its stored `policy`) either
   serves the next queued writer or renews the readers that asked to renew — fulfilling each renewal
   `promise` whose **`future` payload is the object's *current* location**, so a renewed reader never
   holds a stale pointer even though the object may have moved.

## Scheduling (owner's decision)
- Pending **writes** live in a `std::priority_queue` keyed by `(priority-as-long-long, arrival)`; the
  owner pops the winner and fulfills its `std::promise<Lease<T, write>>`.
- **Waiting readers** hold renewal futures fulfilled in a wave after a write.
- The stored **`policy`** decides, at each hand-off, whether the next grant is a queued writer or the
  reader renewal wave (`interleave` vs `writes_first`).

## Negative priority = immediate-write (the one queue-bypass)
Sometimes a write is *so* urgent it cannot wait for the queue or even the next scheduling tick (an
alarm, a shutdown flag, a correction that everything else is now reading wrong). A **negative
priority** — spelled with the named constant **`o.rwrite<memory::immediate>()`** (`memory::immediate == -1`)
— is that escape hatch, and **only** that:

1. It **does not enter the priority queue** — it goes to the front of everything, above every
   non-negative level.
2. It **preempts the active writer**: the owner trips that writer's stop token, so its lease flips to
   `expired()`. The active writer, seeing `!w.valid()` at its next check, **stops touching `w.write()`
   and waits** — it does *not* destroy the lease. (A writer that never loops on `valid()` runs its
   current lease to completion first — cooperative, never forced.) The owner internally suspends that
   writer's lock.
3. The immediate-write acquires exclusively, does its work (possibly moving the object), releases.
4. **The preempted writer resumes transparently** — the owner restores its lock and `w.valid()` flips
   back to true, with `w.write()` now pointing at the object's *current* location. The writer just
   continues; there is **no re-acquire call**, and this happens *before* the owner touches the queue.
   From the queue's and every priority's point of view nothing changed — one writer was briefly paused
   and then continued.

So an immediate-write is the *only* thing that invalidates another writer, and its cost is bounded:
it borrows the object for one write and hands it straight back to whoever it interrupted. Every
non-negative priority respects the queue + `policy` in full.

## Compile-time-only write-renewal (deliberate friction)
A write lease is **one-shot by default**: when it ends the owner owes the writer nothing. A writer
that intends to **write repeatedly** must say so *to the compiler* — a distinct write-lease type
(`Lease<T, memory::write_renewable>`, obtained from a distinct accessor), never a runtime flag. That
makes "I keep writing this shared thing" a legible code smell: if you must, you probably should *be
the owner*, or you are explicitly in the "the hive is owned by no one and worker threads update it"
case — and you had to declare it.

## What the standard library provides (all of it)
| role | standard type |
|---|---|
| the object + its storage | the value itself |
| the reader/writer gate | `std::shared_mutex` |
| a read lease | `std::shared_lock` |
| a write lease | `std::unique_lock` |
| "please stop reading" / "immediate-write: yield the write" | `std::stop_source` / `std::stop_token` (one source per read-generation; one per active write) |
| "you may re-lease; here's the new location" | `std::promise` / `std::future` |
| write scheduling | `std::priority_queue`, keyed by the compile-time priority value |

## The cheatah surface (and nothing more)
`import memory` exposes exactly: the type `Owner<T>` (one template argument, always); the factory
`own(value)` / `own(value, memory::writes_first)` (policy fixed here, at construction); the requests
`owner.rread()` and `owner.rwrite<priority>()` (priority a compile-time int or the user's enum value;
negative = immediate), each returning a `memory::Request<Lease<…>>` redeemed with `.acquire(on_interrupt)`;
the handle `Lease` accessed via `read()` / `write()`, and on a read (or a preemptible write) lease
`valid()` / `expired()` (and `on_interrupt()`); the constant `memory::immediate` (`= -1`); and the
`memory::read` / `memory::write` / `memory::write_renewable` tags and the `memory::policy` enum. It
exposes **no** raw pointer to the object, **no** bare-lease accessor, **no** way to write without a
lease, **no** `std::promise` (owner-only), **no** hand-spelled class template parameters beyond `T`, and
**no** copy/clone/leak path. The words "future"/"promise" never surface — only `Request` and
`.acquire()`. Misuse of *access* is unrepresentable.

## No new cheatah vocabulary — `Request` hides the future
Cheatah does **not** gain `future`/`promise` types right now. `rread()`/`rwrite<…>()` return a
`memory::Request<Lease<…>>`, which codegen maps like any module type. Its only cheatah-visible operation
is **`.acquire(on_interrupt)`** (→ the lease); the wrapped `std::future` and the owner's `std::promise`
stay entirely inside the C++ backend. (Design history: a hand-rolled `pending`(`.claim`) → bare
`std::future`(`.get`) → a `std::future` *subclass* → this composition wrapper with `.acquire()` — the
last keeps future/promise terminology out of the language, lets the interrupt callback ride into the
blocking call, and needs no `friend`.)

## Safety properties (why this is memory-safe today, thread-safe tomorrow)
- **No use-after-move:** the writer touches no byte until reads drain to zero; renewed readers get the
  object's *current* location through the future.
- **No self-deadlock:** a write request expires the requester's own read lease, so it never waits on
  itself.
- **No dangling / no copy footguns:** the only handle is a lease; the object is never copied.
- **Tunable liveness:** `interleave` (fair) vs `writes_first` (writer-drain); the write priority
  orders queued writers; a negative-priority immediate-write can jump the whole queue but must hand
  the object straight back to the writer it interrupted. Any resulting starvation is an owner's
  declared choice, not a safety bug.
- **Bounded preemption:** an immediate-write invalidates *one* active writer for the length of *one*
  write and then restores it — it cannot lose the interrupted writer's work or reorder the queue.

## Open / deferred
- **Runtime priority.** Priority is a *compile-time* accessor argument (`write<N>()`), so a call site
  bakes in its urgency. A runtime-chosen priority is deferred until a real use appears; the
  compile-time form covers the cases we have and keeps the queue key trivial.
- A fully **custom** scheduling comparator (a callback) is deferred — a compile-time integer/enum
  priority (higher = higher, negative = immediate) covers the real cases and needs no cheatah function
  values.
- **Nested / re-entrant immediate-writes** (a negative-priority write preempted by a *second* one)
  stack LIFO — each restores the one below it. Pinned as a contract; revisit if a real use appears.
- **Custom decorated leases.** `read()`/`write()` are deliberately the access seam so a user can later
  define a lease subclass that decorates access. The dispatch mechanism (virtual methods vs a CRTP
  base so it stays zero-overhead and concept-constrained, in line with cheatah's no-vtable leanings)
  and how an owner is told to hand out the custom type are **deferred** — the base API is shaped for it
  now (access goes through `read()`/`write()`, not a raw `get()`), but the extension point isn't built.
- The actual C++ implementation follows **after this review**; the companion
  `tests/memory_test.cpp` pins the behaviour the implementation must satisfy.
