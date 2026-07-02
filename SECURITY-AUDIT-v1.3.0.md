# cheatah — Security Audit, v1.3.0-alpha

Copyright (c) 2026 BigBrain LLC. Lead engineer and producer: Joshua Doucette, on behalf of BigBrain LLC.

**Scope of this audit:** the memory-safety guarantee for the cheatah standard library and
compiler in this repository — specifically, *can a user leak memory from pure cheatah?* This
release makes the answer **no**: outside a `cpp { … }` escape hatch, pure cheatah code cannot
leak heap memory or an OS handle.

## The guarantee

> **Pure cheatah code cannot leak heap memory.** Every value is a value type that frees by
> scope, and every *stateful* resource is handed to cheatah as an **owning RAII guard** that
> releases on scope exit. The heap-allocating raw handle APIs are **not reachable from cheatah**
> at all — they are C++-only. The single way to leak from cheatah is to write raw C++ inside a
> `cpp { … }` block, which is explicitly outside the guarantee.

Deterministic cleanup is provided by the `with resource [as name] { … }` statement and by the
guard types themselves (a guard held as a plain `let` also closes at scope exit — `with` is for
readability, not correctness).

## Every allocation/ownership site reachable from cheatah

| Site | Module | Representation | Reachable-from-cheatah surface | Leakable from cheatah? |
|---|---|---|---|---|
| element buffer | `ndarray` | `std::shared_ptr<std::vector<T>>` (acyclic; views share) | value type | **No** — reference-counted |
| file | `io` | `io::File` wrapping `std::fstream` | `io.open()` → `File` (RAII) | **No** — dtor closes |
| TCP fd | `socket` | integer fd | `socket.open()`/`serve()` → `Conn`/`Listener` (RAII); raw fd API also visible | fd only (a *resource*, not heap memory); guard closes it |
| TLS session | `tls` | entry in a `std::map<id,Session>` | `tls.open()` → `Conn` (RAII) **only**; `client_connect`/`send`/`recv`/`close` moved to `tls_lowlevel.hpp` (C++-only) | **No** — the allocator is unreachable from cheatah |
| WebSocket session | `websocket` | `new Session` (heap) | `websocket.open()`/`open_url()` → `Client` (RAII) **only**; `connect`/`connect_url`/… moved to `websocket_lowlevel.hpp` (C++-only) | **No** — the allocator is unreachable from cheatah |
| JSON tree | `parsers.json` | `std::vector`/`std::span` (value/view) | value types | **No** |
| lists/dicts/strings | codegen | `std::vector`/`unordered_map`/`std::string` | value types | **No** |

## Findings and the fix shipped in v1.3.0-alpha

1. **Codegen is value-semantic (verified).** `compiler/codegen.cpp` emits only value types and
   STL containers — no raw `new`/`delete`, no manual pointer arithmetic — so use-after-free,
   double-free, and leaks are off the table for compiler-written code. The only raw allocation in
   generated output comes from a `cpp { … }` block.

2. **RAII guards on every stateful module (shipped this release).** `socket::Conn`/`Listener`,
   `tls::Conn`, `websocket::Client`, and the pre-existing `io::File` are move-only owning types
   whose destructors release the resource on every exit path (return, break, exception). `with`
   binds one for a block.

3. **The heap-allocating raw APIs were reachable from cheatah — now closed (the core fix).**
   Before this release the raw handle constructors sat in the same importable namespace as the
   guards, so a cheatah program could call `tls.client_connect(...)` or
   `websocket.connect(...)` (which `new`s a `Session` / inserts a `map` entry) and leak by never
   calling `close()`. This release **moves the raw handle API of `tls` and `websocket` into
   C++-only `*_lowlevel.hpp` headers** that cheatah's module resolver does not surface. A cheatah
   program that calls them now **fails to compile** (verified: `no member named 'client_connect'
   in namespace 'cheatah::tls'`; `no member named 'connect' in namespace 'cheatah::websocket'`).
   The stdlib's own `requests` module was rewritten to use the `tls.open` guard accordingly.
   - `socket`'s fd-based API remains cheatah-visible: an unclosed fd is an OS-*resource* leak, not
     a heap-memory leak, and low-level socket programming from cheatah is a supported use; the
     `socket.open`/`serve` guards are the recommended, leak-safe path.

4. **The `cpp { … }` escape hatch is the sole exception (by design).** `compiler/ast.hpp`'s
   `RawCpp` node emits verbatim C++; memory safety there is the author's responsibility, exactly
   as in C++. This is the one documented way to leak from cheatah source.

## Evidence

- **Static:** a `.purr` calling the raw heap constructors fails the C++ backend compile (proof
  the surface is unreachable). Grep confirms no `new`/`delete` in generated code.
- **Dynamic:** the full unit suite (328 tests) plus the guard round-trips (`socket` loopback,
  `tls` vs `openssl s_server`, `websocket` vs a real Node `ws` server) run under **AddressSanitizer +
  UndefinedBehaviorSanitizer** (the `asan` preset) and **Valgrind memcheck**
  (`security/run-valgrind.sh`) — memory errors, UB, and definite/indirect leaks fail the build.
  A `with`/guard `.purr` program under Valgrind reports **definitely lost: 0 bytes**.

## Residual, non-goals, and caveats

- **fds via the raw `socket` API** can still be leaked from cheatah (forgetting `close`), like
  Python's `open()` without `with`. This is a resource leak, not heap memory; use the guards.
- **`cpp { … }`** is unbounded by design.
- The guarantee is about **memory/handle leaks**, not the broader trust model — cheatah is
  single-trust with no sandbox (see `SECURITY.md`).
