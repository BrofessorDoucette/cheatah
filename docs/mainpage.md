# cheatah 🐆 standard library

<div class="cheetah-slogan">🐱 <em>Programs so fast they purrrrrrrrrrrrr like a kitten.</em> 🐆</div>

**cheatah** is Python for people who care about performance: you write `.purr`
source, compile it with `purrc` (lexer → parser → codegen → C++ → `.so`), and run
it on the headless `cheatah` host. This site documents the **standard library**.

> **New here?** If you want to know *why* cheatah exists despite the world already having
> too many languages, read **[Why cheatah?](why.html)**. If you'd rather just start
> writing code, jump to **[Getting started](getting-started.html)**.

## How to read these docs 🐾

Every standard-library function carries a Doxygen/Javadoc comment with a consistent
contract, so you always know what a call costs:

- <b>`@param` / `@return`</b> — what goes in and comes out.
- <b>`@complexity`</b> — the **runtime complexity** (Big-O) of the call.
- <b>`@alloc`</b> — the function's **heap behavior**: `none`, or what it allocates
  (`allocates the result`, `allocates a temporary`, scratch buffers, …). Memory
  behavior is a first-class part of the contract — cheatah exists for memory safety.
- <b>`@test`</b> — a link to the unit test that exercises the function.

> <b>On `@complexity`:</b> this is the *algorithmic* (Big-O) cost — how the work
> scales with input size. Wall-clock time is **machine-dependent**, and the
> constant factors can shift with the **C runtime / standard library** linked at
> run time. Treat Big-O as the contract; benchmark for absolute numbers.

> **Examples in the docs:** function pages are gaining a consistent
> <b>`@par Example`</b> block with runnable code — every `plot` function page
> carries one today, and the same convention is arriving across the extensions.

The entire library is verified on every QA-gate run under **AddressSanitizer**
(the `asan` preset) and **Valgrind** (`security/run-valgrind.sh`), with **100% line
and function coverage** of the stdlib.

See the **[Performance](performance.html)** guide for how cheatah delivers
hand-written-C++ speed — zero-cost generic abstractions, declarative SIMD, and
automatic string-concatenation optimization — at the cost of compile time.

## Modules 🐆

| Module | What it gives you |
|--------|-------------------|
| `aead`      | Authenticated encryption — ChaCha20-Poly1305 and AES-GCM (hardware-accelerated), from scratch. |
| `builtins`  | Always-available built-ins: `len`, `ord`/`chr`, `hex`/`oct`/`bin`, conversions, `hash`. |
| `datetime`  | Civil date & time values and formatting. |
| `ed25519`   | Ed25519 public-key signatures (RFC 8032), from scratch — sign, verify, keygen. Backs cheatah's [module-integrity](security.html) check. |
| `hashlib`   | SHA-256/SHA-512 digests (hex and raw), HMAC, HKDF (RFC 5869), and Base64. |
| `io`        | `print`, `str`/`repr`/`format`, `input`, file I/O. |
| `linalg`    | numpy-style linear algebra on `ndarray`, SIMD-accelerated. |
| `math`      | Scalar math (pure, allocation-free) + `abs`/`min`/`max`/`pow`. |
| `memory`    | Ownership for shared state — `Owner<T>` with the request → acquire → lease flow (drain-before-write, write priorities, `memory.immediate`). The blessed way to share across threads. |
| `ndarray`   | N-dimensional arrays **generic over the numeric element type** (deduced from the literals), with broadcasting + declarative SIMD. |
| `os`        | Environment, process, and filesystem (`os.path`) helpers, plus `os.urandom` (CSPRNG). |
| `p256`      | NIST P-256 ECDSA signatures, from scratch — used to verify server certificates in `tls`. |
| `parsers`   | From-scratch input parsers — `parsers.json` (SIMD-accelerated JSON), `parsers.xml` (slab-DOM), `parsers.url`, and `parsers.html`. |
| `random`    | Pseudo-random numbers and selection (per-thread engine — draws never race). |
| `regex`     | From-scratch **linear-time** regular expressions (lazy DFA) — immune to ReDoS by construction. |
| `requests`  | A pure-cheatah HTTP client (the first `.purr` stdlib module). |
| `socket`    | TCP sockets — a small BSD-socket wrapper (Python-`socket`-flavored); owning `Conn`/`Listener` guards for `with`. |
| `statistics`| Mean, median, variance, standard deviation. |
| `string`    | Text ops + Python's `string` constants. |
| `sys`       | Command-line arguments (`sys.argv`), Python-style. |
| `thread`    | Real OS threads — `thread.spawn(f, args...)` → a join-on-destroy `Thread` guard; share mutable state through `memory.Owner` ([the contract](threading.html)). |
| `time`      | Monotonic / wall clocks and sleeping. |
| `tls`       | A from-scratch **TLS 1.3 client** (no OpenSSL); owning `Conn` guard for `with`. |
| `websocket` | A from-scratch <b>WebSocket (RFC 6455) `wss://` client</b> over `tls`; owning `Client` guard for `with`. |
| `x25519`    | X25519 Diffie–Hellman key agreement (RFC 7748), from scratch. |

`hashlib`, `ed25519`, `x25519`, `p256`, and `aead` are the from-scratch crypto
primitives the `tls` client is built on — no OpenSSL anywhere in the stack.

Browse the **Files** and **Namespaces** tabs above for the full per-function
reference, or start from a module's header (e.g. `math.hpp`, `linalg/routines.hpp`).

---

<div class="cheetah-slogan">🐆 Built for speed. Guarded for safety. 🐱</div>

<p class="cheetah-colophon">C++ API parsed with <a href="https://www.doxygen.org/">Doxygen</a>; this site is rendered by cheatah's own documentation generator.</p>
