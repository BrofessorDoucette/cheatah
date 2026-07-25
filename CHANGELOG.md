# Changelog

All notable changes to cheatah. This project is **alpha** — expect breaking
changes between releases.

## v1.7.0-alpha (2026-07-25) — errors grow a kind, the Biome Standard, and documentation you can trust

Exception handling becomes real error *handling*: errors carry a **kind**, `except` can match on it
across multiple handlers, `finally` runs on every exit path, and re-raise works — no more silent
terminate. The ecosystem gains the **Biome Standard**: one semantic version naming the set of
component releases tested to work together, with an append-only definition, a source-retention
guarantee, and biome resolving every fetched tag from it. And the documentation contract stopped
being a convention: every `@alloc`/`@complexity` claim in the standard library was audited against
the implementation — **transitively**, callees included — and a new gate now enforces the tags
forever.

### Language — errors carry a kind
- **`raise` / `except` / `finally` matured**: errors carry a kind, multiple `except` handlers match
  on it, `finally` runs on every exit path (including unwinding), and re-raise propagates the
  original error. A failing program now reports its error instead of silently terminating.
- `index()` returns a const reference (no more deep copy on every subscript, unblocking the
  optimizer); list slices `memmove`; Python floor-mod on integers; user programs unroll.

### The Biome Standard — one version for the tested-together ecosystem
- **`standard = "0.1.0-alpha"` in `cheatah.toml` is now the one version users track.** It names the
  exact toolchain + extension releases tested to work together (this release: cheatah
  `v1.7.0-alpha` + cheatah-gpu `v0.5.0-alpha`); biome resolves every `GIT_TAG` it writes from the
  standard, refuses extensions that aren't members of your standard, and gains a `biome standards`
  command. The old hardcoded `v0.1.0` extension pin is gone; `[cheatah] version` survives as a
  manual toolchain override.
- **The standard's major version is a promise about user code**: it only ever increments when
  programs cannot carry forward (a forced security change, or a language change so fundamental the
  ecosystem's APIs all moved). Member-to-member breakage absorbed inside the set is at most a
  minor. The full contract, worked examples, and the community practices that keep majors rare are
  in `docs/biome.md`.
- **Nothing is ever stranded**: standards are append-only (`standards/*.toml`, drift-checked
  against biome's in-source table by QA gate stage 3d); release tarballs attach to every GitHub
  release; `scripts/archive_standard.sh` snapshots a whole standard (definition + git-archive of
  every member at its pinned tag); and a standard is only ever deprecated for an unpatchable
  security flaw, with a public advisory. biome itself is `0.2.0-alpha` and finally has tests
  (a 12-test CLI round-trip suite).

### Documentation — audited to be true, then gated to stay true
- **Every `@alloc` and `@complexity` tag in the stdlib was verified against the implementation,
  transitively** — a function's tags now account for everything its callees allocate and cost.
  Dozens of falsehoods fixed, among them: HMAC/HKDF "fixed scratch buffers" that are really
  message-sized; `hkdf_expand` was not `O(length)`; ed25519 `sign` runs *two* base-point
  multiplications and hashes the message twice; `regex` unanchored search is worst-case O(n²)
  (still ReDoS-immune — never exponential); TLS `recv` drains every buffered record, and a custom
  CA file is parsed per call; `svdvals` allocates the full SVD workspaces; the `memory` module's
  request/acquire docs described blocking on the wrong side. Dead `@test` references (tests that
  never existed) were fixed across the library — 11 in `builtins` alone.
- **A new `@concurrency` tag** documents blocking behavior and thread-safety where it matters
  (memory, thread, socket, tls, websocket, random, io), and `@warning` marks real hazards (nonce
  reuse, TOCTOU, `insecure=true`, shell interpretation).
- **`scripts/doc_tag_lint.sh` joins the QA gate**: every public stdlib function must carry
  `@complexity`, `@alloc`, and a `@test`/`@crtest`/`@systest` link, or the push is blocked. The
  documentation site now also covers **extension APIs** — cheatah-gpu's full `gpu.*` surface joins
  the sidebar and search index.
- The regex audit found (and this release fixes) a real engine bug: patterns that can match empty
  at end-of-input (`a*$`) never matched.

### Coverage — the denominator now tells the whole truth
- **`parsers.html`, the compiled JSON DOM (`parsers/json/json.cpp`), and `sys` were shipped but
  invisible to the 100% gate** — never compiled into a coverage binary, so the green 100% silently
  excluded them. All three now carry full suites (tokenizer/entity edge cases and a malformed
  battery for HTML; a 23-input rejection battery and the 1000-deep nesting cap for the JSON DOM;
  argv fail-safes for sys) and sit inside the measured denominator at 100% lines and functions.

### Networking — faster downloads
- **Download throughput fixed**: connected sockets are tuned, hardware AES is preferred for TLS
  records, and buffered records are drained per read. New loopback/net benchmark harnesses
  (`scripts/tls_loopback_bench.sh`, `scripts/net_bench_compare.sh`) keep the numbers honest.

### Linalg
- Real `conj_transpose` instantiations ship, and the out-param kernels validate shapes
  allocation-free.

## v1.6.0-alpha (2026-07-17) — a faster transpiler, a parallelized QA gate, and a full security audit

The `purrc` transpiler is faster and the QA gate is dramatically faster — both without changing a
single byte the compiler emits or a single test it runs. And a full-surface security audit hardened
the whole standard library: twelve findings fixed, headlined by making ECDSA **signing**
constant-time and closing two integer-overflow heap writes in `linalg`.

### Compiler — a faster transpiler, byte-for-byte identical output
- **The lexer is ~1.6× faster and codegen ~1.2× faster**, cutting the full frontend transpile of the
  largest shipped module (`requests`) by ~19%: the token stream is pre-reserved, character
  classification is a branch-free ASCII test instead of locale-aware `<cctype>`, the codegen symbol
  tables are hash maps instead of red-black trees, and the parser dispatches keywords through one
  guarded switch. A **golden-master** harness asserts the emitted C++ is byte-identical across a
  corpus of real programs, so every optimization is provably behavior-preserving.

### QA gate — same checks, a fraction of the wall-clock
- **The gate runs its test, sanitizer, and benchmark stages in parallel** (`ctest --parallel`, a
  gtest-sharded Valgrind, a sharded benchmark smoke pass and a pair-sharded perf gate) and pipelines
  the build-independent stages (coverage/docs/cppcheck) behind the build — the same tests, the same
  `-O3 -march=native` binaries, just concurrent. On a 20-core box the unit suite dropped ~139 s → ~15 s
  and Valgrind minutes → ~31 s. A `ccache` launcher absorbs the repeated builds when present.

### Security — full audit, twelve fixes (see SECURITY-AUDIT-v1.6.0.md)
- **ECDSA signing (P-256 / P-384) is now constant-time.** The secret-scalar comb no longer branches
  on, or indexes the table by, the secret nonce/key bits, and its point arithmetic is branch-free —
  closing a local timing side-channel that could leak nonce bits. Verify paths (public data) are
  unchanged; a differential self-check confirms the constant-time ops match the reference on every case.
- **Two `linalg` integer-overflow heap writes are fixed.** `matrix_power` and `kron` formed a product
  of two dimensions before the overflow check, so a wrap under-allocated and the kernel wrote out of
  bounds (ASan-confirmed for `matrix_power` via a broadcast view); both now route through the checked
  multiply and throw on overflow.
- **The JSON and XML parsers no longer overflow the stack on valid deep input** — JSON caps container
  nesting during the validated parse (its owning tree's destructor was the sink); XML's `text()` is
  iterative. **`requests` chunked/header decoding is O(n) instead of O(n²)**, closing a remote
  CPU-exhaustion DoS. **`regex` bounds its parser recursion, epsilon-closure, and lazy-DFA cache**
  against crafted patterns (match time was already linear).
- **Supply-chain hardening for the coming package manager:** `purrc` now allowlists a module header's
  `cheatah-link:` flags to genuine linker inputs (a malicious dependency could otherwise run code on
  the consumer's build host), and env-enforced strict mode refuses an argv trust-anchor substitution.
  Plus bounds on the RSA verify exponent, the AEAD single-message length, and the TLS handshake flight.
- All prior hardening (TLS X.509 validation, WebSocket frame bounds, `requests` caps, P-256 on-curve,
  RSA `e=1`) was re-verified intact, and every fix ships under the 100%-coverage + ASan/UBSan/TSan/
  Valgrind gate.

## v1.5.0-alpha (2026-07-15) — cross-platform macOS/Apple Silicon, a concept-templated linear-algebra library, and TLS chain hardening

cheatah now builds and runs on **macOS (Apple Silicon)** as well as Linux; the **linalg** library
is rewritten as a single concept-templated form — one definition per operation over both the
element and the container (real/complex/host unified) — with no loss of performance; and TLS gains
multi-SAN matching plus ECDSA P-384 / SHA-384 certificate-chain validation.

### Cross-platform — macOS / Apple Silicon
- **The toolchain and standard library build and run on macOS arm64.** `os.module_ext()` returns
  the platform module suffix (`.dylib` / `.dll` / `.so`), used by the launcher, the `biome`
  package manager, and purrc's non-CMake fallback (which also uses `-mcpu=native` and drops `-lm`
  on Apple).
- **Hardware crypto on Apple Silicon**: the AES-GCM path documents and asserts the ARMv8 AES +
  PMULL NEON route (every arm64 Mac ships FEAT_AES/FEAT_PMULL) alongside x86 AES-NI/PCLMULQDQ.
- **`getentropy`** replaces `getrandom` for the CSPRNG — portable across Linux (glibc ≥ 2.25),
  macOS, and BSD.
- **`float()` correctness**: `to_float` is now `template <Number T>`, so `float(0.95)` can never
  route through an integer overload and truncate to `0`.
- The QA gate skips Valgrind on Darwin (broken on Apple Silicon; ASan/UBSan cover it); the TLS
  system test prefers a Homebrew OpenSSL peer over macOS LibreSSL.

### Linalg — one concept-templated definition per operation
- **The whole `linalg` library is rewritten in a two-layer template form** — every routine is
  `template <Field T, template <typename> class Array>` over `Array<T>`, so real and complex (and
  host vs a future device container) are ONE definition instead of hand-duplicated overloads:
  `matmul`, `dot`/`vdot`/`inner` (a `Conj` enum), `solve`/`det`/`inv`, `qr`/`svd`, the eig family,
  and the rest. Public names and results are unchanged; mixing element types or containers is now a
  compile-time error via the concept constraints.
- **Shared numeric primitives**: the multi-accumulator reduction (`reduce_lanes`) is factored once
  in `ndarray` and reused by `sum`, `dot`, `trace`, and Householder QR; the LU preamble folds into
  one `lu_prepare`.
- **No performance regression** — the templates monomorphize to the same machine code; several ops
  are marginally faster (a dropped throwaway zero-fill). `linalg` still matches/beats Eigen on
  dense routines and `fixarray` still beats GLM on the fixed-extent types (the QA perf gate).

### TLS fix — every subjectAltName is matched, not just the first
- **X.509 SAN parsing stopped after the first `dNSName`** (a shared loop-bound variable in the
  DER walk), so any host matched by a *later* SAN entry was refused as "certificate is not valid
  for host". Multi-SAN certificates are the norm on CDN-shared hosts — `https://fastly.com`,
  `https://www.fastly.com`, and `https://github.io` all failed while the first-SAN host worked.
  All SAN entries are now parsed and matched (`CheatahX509.ParsesAllSubjectAltNames`,
  `.MatchesLaterSan`).

### TLS feature — ECDSA P-384 and SHA-384 certificate chains validate
- **New `p384` module**: NIST P-384 (secp384r1) ECDSA *verification*, sharing a width-generic,
  concept-constrained template core (`p256/ec_core.hpp`) with `p256` — the same battle-tested
  Montgomery arithmetic and Jacobian group law, instantiated at 6 limbs. Verified against the
  RFC 6979 A.2.6 known-answer vectors (SHA-384 *and* SHA-256, pinning both hash-truncation
  semantics).
- **`hashlib.sha384` / `hashlib.sha384_digest`**: SHA-384 via the existing SHA-512 core with its
  own IV, NIST-vector- and OpenSSL-cross-checked.
- **Chain validation** now verifies `ecdsa-with-SHA384` and `sha384WithRSAEncryption` signatures,
  dispatching the ECDSA curve by the **issuer key's** named-curve OID and the hash by the
  signature OID — real CA chains mix them (Sectigo signs a P-256 intermediate with a P-384 root).
  api.github.com and cdn.jsdelivr.net now validate; SHA-512 and rsassa-PSS chain signatures still
  **fail closed** by design.
- **`ecdsa_secp384r1_sha384` (0x0503)** is offered in `signature_algorithms` and verified in
  CertificateVerify, so P-384 *leaf* certificates handshake too (live `openssl s_server` system
  test).

## v1.4.0-alpha (2026-07-10) — smaller memory footprint: opt-in sized integers, plus fixarray + from-import ergonomics

A footprint-and-ergonomics release. Integers gain **opt-in fixed widths** (`i8`…`u64`) so a column,
struct, or `ndarray` can store 1–4 bytes per element at the same compute speed, with `int` still the
64-bit default. `ndarray.astype` builds narrow-element arrays, the `fixarray` fixed-extent
vector/matrix module becomes callable directly from cheatah (including module-qualified type
declarations), and `from … import …` brings in structs, enums, and functions as prefix-free
first-class objects.

### Opt-in sized integer storage types — smaller memory footprint, same speed
- **Declare a narrow width where footprint matters.** Any type annotation may now be an
  explicit-width integer — `i8`/`i16`/`i32`/`i64`, `u8`/`u16`/`u32`/`u64` — so a `list<i32>` stores
  4 bytes per element instead of 8, a `dict<str, u8>` keeps 1-byte values, an `array<i16, N>` and
  `ndarray<i16>` carry narrow elements, and a `struct` of `u8` fields **packs** (two `u8`s → 2 bytes,
  not 16). Proven in-language with `sizeof`. Previously every integer was a 64-bit `long long`,
  everywhere.
- **`int` is unchanged and still the default.** It stays `long long` (64-bit), so standalone
  integers — loop counters, `++`/`--`, literals — never change or slow down. Narrowing is **opt-in
  per declaration**; nothing narrows implicitly.
- **Three spellings, one type.** Each width is nameable as our short form (`i32`), the long form
  (`int32`), or the original C library name (`int32_t`) — all the **same** `<cstdint>` exact-width
  type by construction, so use whichever you prefer.
- **Zero runtime cost, portable.** A width lowers straight to a `std::int32_t`/`std::uint8_t`/…; the
  storage is contiguous and SIMD-friendly and arithmetic still promotes to 64-bit for free — only
  the *stored* form is narrow. Standard C++20, no tagging/boxing/bit-packing.
- **Semantics.** Narrow storage wraps at its width and a 64-bit result truncates on store (as in C /
  NumPy fixed-width types); a literal initializer that does not fit is a **compile-time error**, the
  only check and it costs nothing at runtime.

### `ndarray.astype` — build a narrow-element array
- **`arr.astype(<width>)`** converts an ndarray's element type — numpy's `a.astype(dtype)` — so
  `ndarray.array([1, 2, 3]).astype(i16)` is a `basic_ndarray<std::int16_t>` (2 bytes/element, not 8).
  Widening is exact; narrowing truncates at the target width; complex→real is a clear compile error.
- **A declared narrow ndarray type drives construction:** `let a: ndarray<i8> = ndarray.array([…])`
  converts for you (no explicit `.astype` needed). Narrow (`i8`/`u8`) elements print as **numbers**,
  matching the rest of the language. Narrowing/widening follows C / numpy fixed-dtype semantics —
  signed narrowing wraps two's-complement, unsigned is modulo 2^bits, float→int truncates toward
  zero — and every case is covered by tests asserting the exact printed values.

### `fixarray` is now callable from cheatah
- The fixed-extent vector/matrix module (`import fixarray`) can be used directly from a `.purr`
  program: **construct** (`fixarray.vec3f(1.0, 2.0, 3.0)`, `fixarray.Fixed<f32, 3>(…)`, narrow
  `fixarray.Vec<u8, 3>(…)`), and call its operations (`fixarray.dot`, `cross`, `normalize`,
  `matmul`, `+`/`-`/`*`). Previously the module was reachable only from C++.
- **Module-qualified types now work in `let` and struct-field annotations** — `let v: fixarray.vec3f`,
  `let m: fixarray.Fixed<f32, 3>`, `struct Body { pos: fixarray.vec3f }` — closing a gap where a
  dotted type (`state.State`, `fixarray.Fixed<…>`) could only appear on function parameters/returns.
  Numeric template extents (`Fixed<f32, 4, 4>`) are also accepted in parameter/return positions.

## v1.3.0-alpha (2026-07-03) — deterministic resource cleanup, ownership + native threads, first-class crypto/networking

The standard-library release: cheatah gains a `with` statement and owning RAII guards, an
ownership/borrow engine (`memory`) feeding real OS threads (`thread`), the crypto + networking
modules become first-class (and 100% tested against real peers), and pure cheatah is now provably
leak-free. Copyright is held by **BigBrain LLC** (lead engineer and producer: Joshua Doucette, on
its behalf); MIT-licensed.

### New `memory` module — an ownership/borrow engine (`Owner` / `Lease` / `Request`)
- **`memory.own(value) -> Owner<T>`** takes SOLE ownership by **moving** the value in (it is
  *consumed*, never copied); an `Owner` is non-copyable and pinned, so its object never moves and a
  borrow can never dangle. `T` is the only type you spell — the scheduling policy is a constructor
  argument.
- **Every access is a request → acquire → lease.** `o.rread()` / `o.rwrite<priority>()` return a
  `Request`; `.acquire()` blocks until the owner grants a **`Lease`** — the only handle to the object.
  Read leases are shared (they coexist); write leases are exclusive.
- **Setters and symmetric getters, concept-gated on the owned type.** `write` is a setter (never
  returns a mutable object): `w.write(value)`, and — deduced — `w.write(index, v)` for sequences and
  `w.write(key, v)` for maps. `read` mirrors it: `r.read()`, `r.read(index)`, `r.read(key)`, plus
  `r.read_front()` / `r.read_back()` where the container has them. `read` always returns a **reference**.
- **A hand-rolled priority reader/writer engine** (one mutex + condition variable over explicit state,
  since `std::shared_mutex` can't honor priorities): **drain-before-write** (a write waits for readers
  to release; a reader looping on `valid()` yields), a **priority queue** of waiting writes
  (`o.rwrite<10>()` jumps ahead), and **immediate-writes** (`o.rwrite<memory.immediate>()`, any
  negative priority) that preempt an active cooperating writer, do their work, and let it resume.
  Deterministic-final results under nondeterministic interleaving; ASan/UBSan-clean.

### `regex` — the match is now **owned**, no more borrowed `view`
- **`regex.find(...).text` is an owned `str`** (the library copies the matched bytes), so it is always
  safe to keep, pass, or return — even off a temporary input. The old borrowed `view<str>` (which
  dangled on a temporary — a use-after-free) is **removed entirely**, along with the `view<T>` type
  from the prelude. `.begin`/`.end` offsets remain for callers who want to slice their own input
  zero-copy. cheatah is C++ — *somewhere* we own the string.

### New `thread` module — cheatah gets real OS threads
- **`thread.spawn(f, args...) -> Thread`** runs a cheatah `fn` on a new thread; the returned
  guard is move-only and **joins at scope exit** (plain `let`, `with`, or unwinding — every
  thread finishes before `main` returns). **No `detach`, by design**: the host unloads the
  program's module right after `main`, and an unjoined thread would break the deterministic-
  cleanup guarantee.
- **Copy-in by default; an `Owner` by reference.** Every copyable argument is copied into the
  thread (a worker owns its values — you cannot accidentally share a plain value); a movable
  rvalue is moved in; a pinned, non-copyable `memory.Owner<T>` is the deliberate exception and
  travels by reference — the module's design funnels shared mutable state through the `memory`
  module's request → acquire → lease flow. A non-copyable temporary does not compile.
- **A worker `raise` re-surfaces at `t.join()`** (catch it in-language with `try`/`except`);
  an exception nobody joined for is reported on stderr by the guard's destructor. `joinable()`
  tracks the handle's lifecycle.
- **The threading contract is documented** (`docs/threading.md`): races are the developer's
  responsibility — cheatah keeps per-thread guarantees airtight and pushes sharing toward
  `memory.Owner` (an `Owner` *is* the lock; `memory.own(false)` is a stop latch).

### Codegen
- **Template arguments inside module-qualified type annotations now map like every other
  cheatah type**: a parameter `o : memory.Owner<int>` lowers to the concrete
  `memory::Owner<long long>&` that `memory.own(0)` deduces (previously the `int` leaked
  through raw). Locked in by an emitted-source assertion (`StdlibE2E.ThreadOwnerParamLowering`).

### Quality
- **New ThreadSanitizer gate**: a `tsan` CMake preset (mutually exclusive with the ASan one)
  and a QA-gate stage that runs the concurrency-relevant suites under TSan
  (`QA_GATE_SKIP_TSAN=1` to skip locally) — a data race in the standard library now fails the
  gate.
- **`random` is per-thread**: the once-shared Mersenne Twister is now `thread_local`
  (concurrent draws from spawned workers never race; `random.seed` seeds the calling thread
  only, and each new thread self-seeds from `std::random_device`).

### Docs & tooling
- **Dogfooding, now parallel.** The pure-cheatah doc-render benchmark gains a **parallel** variant
  ([`docs/gen-cheatah/gen_bench_parallel.purr`](docs/gen-cheatah/gen_bench_parallel.purr)) — the read-
  only Doxygen XML lives in one shared `memory.Owner` (coexisting read leases), workers accumulate the
  rendered-byte and (regex-counted) function totals into `Owner`s via exclusive writes, and the result
  is **byte-identical** to the single-threaded run. Four modules cooperating (`parsers.xml` + `regex` +
  `memory` + `thread`): ~1.8× the single-threaded speed and **4.3× CPython**, deterministic. See
  [Performance → Dogfooding](docs/performance.md).
- **VS Code extension → 1.3.0** (aligned with the toolchain): grammar recognizes the full stdlib module
  list (`p256`/`x25519` added) and highlights `constexpr`/`auto`; added a LICENSE file and CHANGELOG,
  refreshed README, dropped the committed `.vsix` build artifact.

### `with` + owning RAII guards (deterministic cleanup)
- **New `with resource [as name] { … }` statement** — binds a resource for the block and runs
  its destructor on **every** exit path (return, break, exception). Lowers to a lean C++ scope;
  it is RAII, not a Python `__enter__`/`__exit__` protocol (any value with a destructor works).
- **Owning guard types** on every stateful module: `socket.Conn`/`Listener` (via `socket.open`/
  `serve`), `tls.Conn` (via `tls.open`), `websocket.Client` (via `websocket.open`/`open_url`),
  alongside the existing `io.File`. A guard held as a plain `let` also closes at scope exit.

### Airtight memory-leak guarantee
- **Pure cheatah cannot leak heap memory.** The heap-allocating raw handle APIs of `tls`
  (`client_connect`, …) and `websocket` (`connect`/`connect_url`, …) are now **C++-only** —
  moved to `tls_lowlevel.hpp` / `websocket_lowlevel.hpp`, which cheatah's module resolver does
  not surface. Calling them from cheatah is a **compile error**; cheatah uses the guards, which
  release automatically. The sole in-language leak path is now a `cpp { … }` block. See
  `SECURITY-AUDIT-v1.3.0.md`.
- `socket`'s fd-based API stays cheatah-visible (an unclosed fd is an OS-resource leak, not heap
  memory); the `socket.open`/`serve` guards are the recommended leak-safe path.

### First-class crypto + networking (now tracked + 100% covered)
Six modules that had lived out-of-tree are now committed, first-class, and held to the full
gate (100% line+function coverage, 100% Javadoc, ASan/UBSan/Valgrind clean):
- **`aead`** — ChaCha20-Poly1305 (RFC 8439) and AES-128/256-GCM authenticated encryption, with a
  runtime-selected AES-NI fast path and a portable fallback.
- **`x25519`** — RFC 7748 Diffie-Hellman, constant-time in the secret scalar.
- **`p256`** — NIST P-256 ECDSA sign/verify with RFC 6979 deterministic nonces, plus SPKI/DER
  parsing (ES256 JWTs and TLS leaf certificates).
- **`tls`** — a from-scratch **TLS 1.3 client** (RFC 8446) built only on the cheatah crypto
  modules — **no OpenSSL**. TLS_CHACHA20_POLY1305_SHA256 / TLS_AES_128_GCM_SHA256, X25519, SNI;
  authenticates the server leaf (Ed25519 / ECDSA P-256 / RSA-PSS) and refuses a peer it cannot
  verify.
- **`websocket`** — an RFC 6455 `wss://` client over the tls stack (client masking, fragmented
  message reassembly, transparent ping/pong).
- **`requests`** — an HTTP/1.1 client (query params, custom headers, redirect following,
  chunked/Content-Length/EOF framing, per-request timeouts, `https://`) and **the first
  standard-library module written in cheatah itself** (`requests.purr`).
- **Coverage is measured against real peers, never a mock**: `tls` against `openssl s_server`
  (each leaf-cert algorithm and record cipher), `websocket` against a real Node `ws` server,
  `requests`/`socket` over a loopback server — all merged into the 100% line+function gate.
  `parsers` is now hand-written C++ (its `.purr` was retired).
- **PEM parsing fails closed**: X.509 certificate bodies decode with a strict base64 mode — a
  non-alphabet, non-whitespace byte rejects the certificate instead of decoding to garbage — and
  the crypto modules now share hashlib's single canonical `to_hex`/`from_hex` (the duplicated
  per-module hex helpers are gone).

### Compiler (`purrc`)
- **Pure-cheatah modules with `struct`s are now fully documented**: purrc emits Javadoc for
  struct **fields**, the synthesized `parsers.json.schema<>` specializations, `module_abi`, and
  default-argument forwarding overloads — so a `.purr` module passes the 100%-Javadoc gate.

### Docs, quality, and copyright
- **Security audit** (`SECURITY-AUDIT-v1.3.0.md`): every heap/handle allocation site is enumerated
  and shown to be value-typed or RAII-guarded; ASan+UBSan and Valgrind report **0 leaks / 0
  errors** over the guards.
- **`@complexity` / `@alloc` tags audited** for correctness across the whole stdlib — e.g.
  `linalg.eig` corrected to **O(n⁴)** (per-eigenvalue inverse iteration), `string.split` to
  O(n·m), and out-parameter products now note the scratch packing of non-contiguous operands.
- **Coverage gate** merges the in-process real-peer system tests and honors a sparing, justified
  `// LCOV_EXCL_LINE` marker for genuinely-unreachable **defensive** branches (three in `tls`: a
  refusal path a conformant peer cannot trigger, which we neither delete nor reach by mirroring a
  malformed server).
- **Documentation** refreshed and tightened repo-wide (module READMEs incl. new `tls`/`requests`
  pages, the porting + security guides, the generated site + VS Code hover DB); every doc page now
  carries the **BigBrain LLC** copyright footer.
- **Docs site restructured**: a three-section sidebar — **Guides**, **Language parity** (a new
  **cheatah ↔ C++** page covering the C++ features usable first-class *without* a `cpp { … }`
  escape hatch, beside cheatah ↔ Python), and **Modules** with submodules (`os.path`,
  `parsers.json`, …) as collapsible dropdowns; a module's classes are now listed on the module's
  own page instead of a separate sidebar column. New **biome** (package manager) and **imports**
  (module resolution) guides, plus richer transpiler examples (multi-module linking; an
  `interface` lowering to a C++20 concept).
- **Copyright** held by **BigBrain LLC** (`LICENSE`, `NOTICE`, README) — lead engineer and
  producer Joshua Doucette, on its behalf. The README links the public ecosystem packages
  (`cheatah-space`, `cheatah-gpu`, `cheatah-plot`).

## v1.2.0-alpha (2026-06-10) — pure-cheatah library modules, language ergonomics, pretty output, editor diagnostics

A large feature release: cheatah can now build **standard-library modules written in cheatah
itself**, the language gained several ergonomics + safety rules, output is readable by
default, and the editor surfaces real compiler errors as you type. Additive — existing
programs keep working.

### Pure-cheatah library modules (`import` a `.purr`)
- **`purrc --emit-library`** compiles a `.purr` into an importable module living in
  `namespace cheatah::<name>` — a signed header (+ a compiled archive in opaque builds),
  verified by a consumer's `purrc` before it compiles against it. **Opaque by default**
  (ships only the API, hides concretely-typed implementations in `libcheatah_<name>.a`);
  **`--transparent`** inlines the generated C++ source into the header (what the first-party
  stdlib uses, so the true code is always visible). purrc verifies a module's SHA-512 (and,
  with `CHEATAH_TRUST`, its Ed25519 signature) and **fails closed on a tampered module**.
- **New first-party `parsers` module** — the first stdlib module authored in `.purr` (empty
  for now; the mechanism is set up). `cmake/CheatahModule.cmake` (`cheatah_add_module`) builds
  it; the QA gate guards that its committed header stays in sync with its source.
- **biome** extension template is now a `.purr` library (opaque by default), and
  `cheatah_add_program(… EXTENSIONS …)` wires a fetched extension's module dir onto purrc's
  search path.

### Language ergonomics + safety
- **An unset value is a bug that does not compile.** A struct is built with a **C++20
  designated initializer** `Point({.x = 1})` (fields you omit **default-initialize**, never
  garbage; an unknown field is an error). A `let` may be declared with **no value**
  (`let total`, `let total: float`), but the compiler tracks it: a never-assigned variable is
  **removed**, and one **used before it is definitely assigned — or only conditionally
  assigned — does not compile**.
- **Multi-line expressions.** Newlines inside `( )` and `[ ]` are now insignificant (Python
  implicit line continuation), and the generated C++ **preserves the source's multi-line
  layout** (readable `.gen.cpp`).
- **`str()` is a builtin** (Python `str()`); `"x" + value` auto-stringifies to the *same*
  minimal C++ as `"x" + str(value)`, and `str(str(x))` collapses to `str(x)`.
- **Dead-variable elimination** is on by default (unused, non-returned locals are removed,
  side-effecting initializers preserved); opt out with **`--no-remove-variables`** or the
  umbrella **`--no-optimize-cpp`**.
- **`--validate-cpp`** infrastructure (a post-codegen, pre-compile hook + a
  `cheatah::purrc::CppValidationException`), wired but not yet enforcing.

### Readable-by-default output
- **`io.print` pretty-prints**: a struct renders on indented multiple lines (recursively),
  and a large **NDArray is abbreviated with `…`** (numpy-style edge items). **New `io.rprint`**
  prints the raw/compact form (a full, unabbreviated array). NDArray is now directly
  **Streamable** (`operator<<`), and every cheatah struct of streamable fields gets an
  auto-generated `operator<<`.

### Editor (VS Code extension)
- **Live diagnostics**: the extension type-checks the open buffer with **`purrc --check`** and
  squiggles real errors — a forgotten `let`, an unresolved symbol, a wrong argument count or
  type — mapped to the `.purr` via `#line` directives (settings: `cheatah.purrc`,
  `cheatah.diagnostics.enable`).
- **Fixed autocomplete/hover for `linalg` and `ndarray`** (a perf-rendering crash had disabled
  the whole provider for those modules).

### Tooling + tests
- **`previously_broken/` regression suite** — bugs that once broke the toolchain, **run FIRST
  in the QA gate** so a reintroduction fails fast. **`library_module_test`** covers
  transparent/opaque emit, import verification, and tamper-fails-closed.

## v1.1.1-alpha (2026-06-10) — 512-bit module integrity + verification benchmarks

A hardening pass on the v1.1.0 integrity feature: the binary and runtime signing path is
now **512-bit throughout**, and the per-tier verification cost is measured, not guessed.

### Signing now uses SHA-512 (512-bit), not SHA-256
- The module **checksum sidecar is now `<module>.sha512`** (sha512sum-compatible), written
  by `purrc --checksum`/`--sign` and auto-verified by the runtime. Ed25519 signatures
  already hashed with SHA-512 internally, so **all** of code- and runtime-signing is now
  512-bit. **SHA-256 stays in the `hashlib` stdlib module** for your own applications — it
  is simply no longer used for signing.
- *Breaking (alpha):* a `<module>.sha256` produced by v1.1.0 is no longer recognized;
  re-run `purrc --checksum` (or `--sign`) to emit the `.sha512` sidecar.

### Verification benchmarks
- New [`tests/benchmarks/integrity_bench.cpp`](tests/benchmarks/integrity_bench.cpp) times
  `verify_module` per tier. Verification is paid **once at load**, never during execution,
  and is **zero-overhead** when off. Representative `x86_64` cost: a 64 KiB module is
  ~0.16 ms with the SHA-512 checksum, ~2.4 ms with a strict Ed25519 signature, ~4.4 ms with
  the signed runtime manifest as well. The SHA-512 pass scales with module size; each
  Ed25519 verify is a roughly fixed ~2 ms (the from-scratch crypto is audit-oriented, not
  throughput-tuned). The numbers now back the **Security** page's Performance section and a
  table in `SECURITY.md`.

## v1.1.0-alpha (2026-06-10) — module integrity: from-scratch crypto + signed binaries

cheatah can now verify a compiled module against **corruption and tampering** before the
runtime loads it, backed by cryptography **implemented from scratch in the standard
library** (no external dependency). Additive and opt-in — existing programs are
unaffected and pay nothing unless they turn it on.

### New stdlib crypto (no external dependency)
- **`hashlib` gains SHA-512** (`sha512`) alongside SHA-256, plus raw-digest forms
  (`sha256_digest`/`sha512_digest`, like Python's `.digest()` vs `.hexdigest()`).
- **New `ed25519` module** — RFC 8032 public-key signatures (`generate`, `public_key`,
  `sign`, `verify`), hand-implemented and **validated byte-for-byte against OpenSSL** and
  the RFC 8032 known-answer vectors. Strict verification rejects non-canonical signatures
  (`S ≥ L`). The runtime links this same code to verify modules.
- **`os.urandom(n)`** — a CSPRNG (`getentropy` / `BCryptGenRandom`), fail-closed.

### Module integrity (opt-in, fail-closed)
- **`purrc --keygen <prefix>`** writes an Ed25519 keypair (secret key created `0600`);
  **`purrc --sign <key>`** signs a built module (writes `<mod>.sig`); **`purrc --checksum`**
  writes a sha256sum-compatible `<mod>.sha256`.
- The **runtime verifies before `dlopen`**: a `.sha256` sidecar is **auto-checked** for
  corruption; with **`CHEATAH_VERIFY=strict`** (or `--verify`) a valid `.sig` from a key
  in the **trust file** (`CHEATAH_TRUST` / `--trust`) is **required**, else the module is
  refused. Verification binds the load to the exact bytes it hashed (`/proc/self/fd`, no
  verify-then-load race), is **non-downgradable** by argv, caps the module size, and is
  **off by default with zero overhead**. See the [Security](docs/security.md) guide; the
  runtime header documents the per-call `@complexity`/`@alloc`.
- **C-runtime compatibility check.** `purrc --runtime` records the build's CPU arch, glibc
  version, and libstdc++ ABI in a **`<mod>.rt`** manifest; the runtime checks it against
  the **live host** before loading (e.g. refuses *"module needs glibc >= 2.39, but this
  host has glibc 2.31"* instead of a cryptic `dlopen` failure). **`purrc --sign-runtime
  <key>`** signs the manifest with a key **separate from the code-signing key**
  (`--trust-runtime` / `CHEATAH_RT_TRUST`), so code authenticity and build-runtime
  provenance are vouched for independently and the two keys are not interchangeable.

### Tooling
- `purrc` and `cheatah` now accept **`--help` / `-h`** (usage to stdout, exit 0), in
  addition to `--version` / `-v`.
- The QA gate now runs **cppcheck** (performance + security) across the repo.
- The VS Code extension highlights cheatah's custom Javadoc tags
  (`@complexity`/`@alloc`/`@test`/`@crtest`/`@systest`) like the standard ones.

### Performance & codegen
- **`ndarray` result buffers are now allocated uninitialized** (a small default-init
  allocator) instead of being zero-filled and then immediately overwritten. The throwaway
  zero pass was invisible on compute-heavy ops but dominated the bandwidth-bound ones:
  element-wise `add` goes from **1.5× *slower*** than NumPy to **1.2× faster** (16384
  elements), `sqrt` ties NumPy at large `n`, and `exp`/`sin` widen to **≈4–7×**. The
  `linalg` results that are as big as their own work — `outer`, `kron`, the conjugate
  transpose — build straight into that buffer and are moved in **zero-copy**, so `outer`
  now **beats Eigen** (was behind) and `kron` is ~1.5× faster than before. Verified ASan +
  Valgrind clean.
- **Generated code now `#include`s a single `cheatah.hpp` prelude** instead of repeating a
  dozen standard-library `#include`s (plus the export macro) at the top of every file. The
  built-in runtime already pulled in most of them; modules still map one-to-one to their
  own headers.

### Docs
- New **“Why cheatah?”** guide (the motivation: a transpiled, statically-typed,
  memory-safe, transparent language for an AI-threat world), reached from the overview.
- The **Security** page documents the integrity feature and threat model; the
  **Performance** page now compares against both NumPy and Eigen and is precise about the
  few routines where their tuned kernels edge ahead. Benchmarks regenerated.

### Notes
- A security audit of the feature was performed; findings (downgrade hardening, fd-bound
  strict loading, size caps, canonical-`S` rejection, key-file permissions) are fixed.

## v1.0.0-alpha (2026-06-09) — per-module namespace aliasing in generated code

The transpiler now shortens every module reference in the C++ it emits. This is a
**codegen-shape change** — the generated `.gen.cpp` looks different (the runtime
behavior of any program is unchanged) — so the version steps to 1.0.0.

### Codegen — every module gets its own short namespace alias
- **The whole program is emitted inside a dedicated `namespace cheatah_program`**, and
  the exported entry point becomes a one-line `extern "C"` trampoline
  (`PURR_EXPORT void purr_main() { cheatah_program::run(); }`). This wrapper is what
  makes the aliasing below *safe*.
- **Each imported module — plus the always-available `builtins` — gets its own distinct
  alias** at the top of that namespace (`namespace io = ::cheatah::io;`,
  `namespace ndarray = ::cheatah::ndarray;`, `namespace linalg = ::cheatah::linalg;`,
  `namespace builtins = ::cheatah::builtins;`, …). The body then reads `io::print`,
  `linalg::solve`, `builtins::len` instead of repeating `cheatah::io::…` everywhere. For
  example `cheatah::io::print(std::string("solve A x = b ->"), cheatah::ndarray::to_string(cheatah::linalg::solve(a, b)))`
  is now `io::print("solve A x = b ->", ndarray::to_string(linalg::solve(a, b)))`.
- **Safe against the global C library.** A module whose name matches a libc/POSIX global
  function (`time`, `random`, `socket`) is still aliased — because the alias lives inside
  `cheatah_program` and resolves to `::cheatah::<name>`, it can never redefine the global
  `::time` / `::random` / `::socket`.
- **Safe against program identifiers.** If the program uses a module's name as one of its
  own identifiers (e.g. a `struct os`, or a parameter `math`), that module is left
  *explicit* (`::cheatah::os::…`) so the alias can't shadow or clash with user code. A
  bare identifier that shadows a module name now correctly resolves to the local (fixes a
  latent bug where `fn bump(math)` emitted `cheatah::math` for the parameter).
- The change is purely in emitted code; **no `.purr` source needs to change** and program
  output is identical.

### Extensions
- The **extension-template** documents the contract: a module must keep everything inside
  `namespace cheatah::<name>` so purrc's per-module alias reaches it, and two extensions
  must have distinct module names.

### Tests
- New **`NamespaceAliasing`** system-level suite (runs in the QA gate) asserts the
  generated C++ itself — each module aliased distinctly, libc-named modules aliased
  safely, and collisions staying explicit — alongside compiling and running each program.

## v0.9.1-alpha (2026-06-09) — tighter string codegen, enum highlighting

A small follow-up: the transpiler emits leaner string-building code, and `enum` now
syntax-highlights in the docs.

### Codegen — self-append builds in place, in one statement
- **A `+`-chain self-append now lowers to a single chained statement** with no
  intermediate `std::string`. `head = head + "Content-Type: " + ctype + nl` becomes
  `((head += "Content-Type: ") += ctype) += nl;` — three in-place appends, no temporary
  per piece, and a string **literal appends as a bare `const char*`** (so `operator+=`
  takes it directly rather than constructing a throwaway `std::string`). Chaining is used
  only where it's valid (`operator+=` returns a reference — std::string and arithmetic
  accumulators, the only types a `+` self-append fires on); a single appended operand
  stays a plain `x += e;`.

### Docs
- **`enum` now syntax-highlights** in the generated docs site — the highlighter's keyword
  set had drifted out of sync with the lexer.

## v0.9.0-alpha (2026-06-09) — enums, the `sys` module, and the `biome` package manager

A scoped, printable `enum` type joins the language; a new `sys` module plus runtime
argument forwarding gives programs their `sys.argv`; and **biome**, a CMake/CPM-based
package manager written largely in cheatah, lands in `pkg-manager/`. The VS Code
extension learns all of it, and gains hover docs for your own same-file definitions.

### Language — enums
- **`enum` declares a scoped, type-safe enumeration** that lowers to a C++ `enum
  class` (not a plain C `enum`): `enum Color { RED, GREEN, BLUE }`. Members are
  reached through the enum name (`Color.RED`), separated by newlines, commas, or
  semicolons, and may carry an explicit value (`enum Status { OK = 0, WARN, FAIL }`,
  with the rest auto-incrementing as in C++).
- Enums compare with `==`/`!=`, work in `match`/`case`, and serve as `struct` field
  and function-parameter types.
- **Enums print for debugging.** `io.print(Color.RED)` shows `Color.RED` (Python's
  style); they also render inside `io.format` and printed lists/dicts. An
  out-of-range value (e.g. from a `cpp { … }` cast) shows `Color(<n>)`.

### Numeric core — a performance pass that now beats Eigen

A focused optimization round on `linalg`/`ndarray`, benchmarked against **Eigen 3.4**
(the reference single-threaded C++ dense-linear-algebra library) as well as NumPy. The
recurring villains were the same few mistakes, hunted down across every function:

- **A heap allocation hidden in a hot predicate.** `is_contiguous` rebuilt the reference
  strides — a `std::vector` allocation — on *every* call, and the products/reductions
  call it per operand. Made it allocation-free; this one fix lifted every contiguous op.
- **Single-accumulator reductions.** A lone running sum serializes on FP-add latency
  (the compiler can't reassociate without `-ffast-math`). `dot`/`norm`/`sum`/`cholesky`/
  `trace` and the symmetric-eigensolver tridiagonalization now use several independent
  accumulators, so `-O3 -march=native` issues SIMD+FMA and reaches memory bandwidth.
- **`matmul` re-streamed B per output row** → 4-row register blocking (real *and* complex)
  reuses each B element four times.
- **`qr` walked columns of a row-major matrix** (stride-n, un-vectorizable) → it now works
  on the transpose so the Householder reductions/updates are contiguous.
- **The symmetric eigensolver's tridiagonalization** kept the active block full-symmetric
  so its matrix–vector product vectorizes (no packed column-stride walk).
- **`make_matrix`/`make_vector` zero-filled a full-size result buffer and then threw it
  away**, replacing it with the computed data — a wasted O(n²) pass that dominated
  memory-bound ops. They now build the result directly from the buffer.

Result: on one core, cheatah **matches or beats Eigen on most dense routines** (matmul,
inv, solve, det, the SVD, the symmetric eigensolver, dot at scale, outer, trace, norm),
and now **beats NumPy/LAPACK across nearly the whole library** — including `outer`, `qr`,
`kron`, and large `norm`, which previously lost. The honest comparison (Eigen ratios, the
NumPy table) and a reproducible benchmark harness ship in `tests/benchmarks/`
(`eigen_compare_bench.cpp`) and `scripts/numpy_compare.py`. A few routines (blocked QR /
Cholesky, the eigenvector path) remain behind Eigen's blocked BLAS-3 kernels — flagged
honestly, not hidden.

### Numeric core — N-dimensional `ndarray` construction

- **`ndarray.array(...)` now builds an array of any rank from a nested list** —
  `array([[1, 2], [3, 4]])` is 2-D, `array([[[1],[2]],[[3],[4]]])` is 3-D, and so on to
  any depth. The shape is inferred from the nesting and the leaf type deduced; a ragged
  list is rejected, exactly as numpy rejects one. (Previously only `reshape` could make a
  >1-D array.) Broadcasting and reductions already worked at every rank.
- **New cross-checks:** the `linalg` routines are verified op-by-op against NumPy by a
  system-test suite that loads editable `.purr` programs from `tests/purrc/linalg_programs/`.

### Standard library — `sys` (command-line arguments)
- **New `sys` module exposes `sys.argv`** — a `list[str]` of the program's
  command-line arguments (`sys.argv[0]` is the program, `sys.argv[1:]` the
  arguments), exactly like Python. Index it, slice it, `len(...)` it, iterate it.
- **The `cheatah` runtime forwards arguments to the program.** Running
  `cheatah app.so one two` populates `sys.argv` via an exported `cheatah_set_argv`
  hook the `sys` module provides; programs that do not `import sys` are unaffected.
  purrc still emits **only** loadable modules — compiled code always runs under the
  runtime.

### Tooling — `biome`, the package manager
- **New `biome` package manager** (in `pkg-manager/`) — most of it written in
  cheatah itself (`biome.purr`, compiled to a module by purrc), driven by a small
  native launcher so it is invoked as `biome <command> <args>` while its compiled
  code still runs only under the cheatah runtime.
- Commands: `init` (scaffold a project with a `cheatah.toml` manifest, generated
  `CMakeLists.txt`, and a CPM bootstrap), `add`/`remove`/`list` (manage optional
  standard-library extensions — `cheatah-gpu`/`cheatah-plot`/`cheatah-space`),
  and `build`/`run` (drive **CMake + CPM**, so the whole build is
  handled by CMake).
- **New `cmake/CheatahProgram.cmake` helper** — `cheatah_add_program(NAME SOURCES
  x.purr …)` compiles a `.purr` to a module with purrc and builds a launcher that
  runs it via the runtime. Downstream projects fetch the toolchain with
  `CPMAddPackage(NAME cheatah …)` and use this helper. A cheatah pulled in as a
  sub-project no longer builds its own test suite (tests default on only when
  cheatah is the top-level project).
- *Status:* core flow works end-to-end (configure → purrc module → launcher, via
  CMake/CPM); wiring third-party extension archives into purrc's link line, and
  publishing the extension repos + release tags, are follow-ups.

### Editor — VS Code extension
- **`enum` is highlighted** (the keyword and the enum name as a type), and the
  extension's IntelliSense understands enums declared in your file: hover an enum
  type or a member (`Color.RED`), autocomplete members after `Color.`, and
  go-to-definition jumps to the declaration.
- **Hover docs for your own same-file definitions.** Hovering a `fn`, `struct`,
  `interface`, or `enum` defined in the open `.purr` file now shows the comment
  written above it (its "docstring") — taking priority over the stdlib database, so
  a local function no longer shows an unrelated same-named header.

### Project
- **Added [ACKNOWLEDGMENTS.md](ACKNOWLEDGMENTS.md)** crediting the open-source work
  (Python, the C++ standard, NumPy/SciPy/Matplotlib, BLAS/LAPACK, Eigen, GLM, …)
  that informed cheatah's design — the author first, then the prior art.

## v0.8.0-alpha (2026-06-09) — Python-3 division + the whole library, documented

The `/` operator is now **true division** (always a float, even `int / int`), matching
Python 3, and integer/floor division moves to an opt-in `//` operator. Alongside it, the
docs site grew to cover the **entire** standard library: every module's README now ships
on its page, and the linalg-vs-NumPy numbers live beside the functions they measure.

### Language — division (**breaking**)
- **`/` is true division.** `6 / 4 == 1.5`, and even an exact `6 / 2` is a `double`. The
  operator lowers to `cheatah::builtins::truediv`.
- **`//` is opt-in floor division**, flooring toward −∞ like Python (`-7 // 2 == -4`,
  `7.0 // 2.0 == 3.0`) — `cheatah::builtins::floordiv`. As a result `//` is **no longer a
  comment**; comments are `#` only.
- *Porting:* a `/` that you relied on for integer division becomes `//`.

### Docs — the whole library on the site
- **Per-module READMEs now render on each module's page.** The examples and prose from
  `stdlib/<mod>/README.md` are merged in as the overview above that module's reference, so
  the worked examples and explanations that previously lived only in the repo are now
  served. (`compiler/PYTHON.md` and the changelog are guide pages too.)
- **The linalg-vs-NumPy comparison moved to the linalg page**, beside the functions it
  measures (element-wise array math to the ndarray page). Each numeric function's
  **Performance** row now shows its own measured vs-NumPy number (µs/op + operand size +
  faster/slower), instead of a generic pointer.
- **Tighter prose.** A pass over the guides trimmed wordiness without dropping facts,
  examples, or the single-threaded-by-design framing.

## v0.7.0-alpha (2026-06-08) — cross-platform: Linux, macOS, and Windows

cheatah now builds and runs on **Linux, macOS, and Windows**. The language, the `purrc`
interface, and every standard-library API are **unchanged** — this release is purely
structural: the compile → link → load pipeline became platform-aware, so a `.purr`
program compiles to the host's native loadable module and the runtime loads it, on each
OS. (Linux is verified end-to-end; macOS and Windows use standard platform APIs behind
detection and want on-device confirmation.)

### Portability
- **One place for the differences** — new `cmake/Portability.cmake` detects, per
  compiler/OS/arch: the native-arch flag (`-march=native`, falling back to `-mcpu=native`
  on Apple Silicon), the loadable-module extension (`.so` / `.dylib` / `.dll`), the
  vector-math library, and the flag/link lists `purrc` passes the C++ backend. The rest
  of the build (and `purrc`) just consumes them, so no `#ifdef` sprawl.
- **`purrc`** consumes the baked flags; spawns the compiler via `fork`+`execvp` (POSIX)
  or `_spawnvp` (Windows); emits the platform module extension.
- **Runtime** validates the host's binary format — **ELF** (Linux), **Mach-O** incl.
  fat/universal (macOS), **PE** (Windows) — and loads via `dlopen` (POSIX) or
  `LoadLibrary` (Windows). *(Fixes "refusing to load … not an ELF shared object" on
  macOS, where a `.so` is really a Mach-O dylib.)*
- **Codegen** exports `purr_main` through a portable macro (`extern "C"`, plus
  `__declspec(dllexport)` so a Windows DLL exposes the entry point).
- **stdlib** — `socket` gains `SO_NOSIGPIPE` (macOS) and a Winsock backend (Windows);
  `os` gains the Windows `getpid`/`setenv` shims.

### SIMD acceleration, per platform
- **Auto-vectorization on every platform** via the detected native-arch flag (the bulk:
  products, factorizations, `sqrt`).
- **Vector transcendentals** through the platform's vector libm where one ships:
  **libmvec** (Linux), **Accelerate** (macOS), opt-in **SVML** (Windows,
  `-DCHEATAH_WIN_SVML=ON`); scalar-but-correct fallback otherwise.

## v0.6.0-alpha (2026-06-08) — winning the numerics: world-class linear algebra + SIMD ufuncs

This release is a ground-up performance pass on the numeric core. We benchmarked every
`linalg` and `ndarray` routine honestly against NumPy/LAPACK, hunted down *why* we lost
where we lost, and fixed the causes. The result: cheatah now **matches or beats** NumPy
on every dense linear-algebra routine measured at the small-to-moderate sizes most
scientific code runs at — products, the LU family, the SVD (and its `pinv`/`cond`/
`matrix_rank` derivatives), and the symmetric eigensolver — and the element-wise math
ufuncs now beat NumPy's too. The recurring villains were two: heap allocations hiding in
element access, and inner loops that couldn't vectorize.

### Linear algebra — algorithms and kernels
- **Killed the per-element heap allocation in matrix/vector extraction.** The extractors
  read elements through `a.at({i, j})`, and the `{i, j}` braced index **heap-allocated a
  `std::vector` per element** — pulling out an n×n matrix did n² allocations before any
  math. Replaced with direct contiguous reads (`memcpy` fast path; strided walk for
  views). This alone is a large speedup across *every* routine.
- **Zero-copy reads for the read-only routines.** `dot`/`matmul`/`outer`/`trace`/`norm`/
  `cholesky`/`kron`/`conj_transpose` (real **and** complex) now operate straight on the
  array's own buffer when it's contiguous — they allocate only their result.
- **`dot` beats BLAS `ddot`.** The reduction was a serial floating-point dependency chain
  that can't vectorize without `-ffast-math`; rewritten with independent accumulators so
  `-O3 -march=native` issues SIMD+FMA. 16384-element `dot`: **280µs → 3.7µs**, from 36×
  *slower* than NumPy to **2.1× faster**.
- **`inv` wins.** It was `n` serial-reduction back-substitutions (un-vectorizable);
  rewritten as a whole-identity block solve whose inner loops are vectorizable SAXPYs.
  32×32: **26µs → 5.7µs**, from NumPy-1.1× to **cheatah 4.2×**. (`det` already won — same
  LU, but its SAXPY update vectorizes; that contrast was the tell.)
- **Symmetric eigensolver: cyclic Jacobi → Householder tridiagonalization + implicit-shift
  QL** (the method LAPACK uses). `eigvalsh` went from losing **10–35×** to **tying
  LAPACK** from 16×16 up, winning decisively below; it also skips the eigenvector
  accumulation it used to compute and throw away. The complex-Hermitian path rides the
  same solver via the 2n real embedding.
- **SVD: one-sided Jacobi → Golub–Reinsch** (Householder bidiagonalization + implicit-shift
  QR), reimplemented entirely **column-major** so the bidiagonalization reflectors and the
  QR's whole-column U/V rotations vectorize. The full decomposition now **beats NumPy
  1.7–1.8×** (so `pinv` wins 1.3–1.4×); a new values-only path **ties LAPACK**, and so do
  `cond`/`matrix_rank`. Replacing the correctly-rounded `std::hypot` in the O(n²) Givens
  rotations with the faster EISPACK `pythag` was the final unlock. 64×64 `svd` went from
  **3505µs (NumPy 18.8×) to a 1.8× win**.
- **New `linalg.svdvals(a)`** — singular values only (≈ `numpy.linalg.svd(compute_uv=False)`):
  skips U/V accumulation and the dominant U/V rotations.
- **Fewer allocations everywhere:** hoisted per-iteration working buffers out of loops
  (`inv`, `qr`, the general eigensolver), factor-the-complex-LU-once in inverse iteration,
  single-copy hand-off into the eigen solvers.

### ndarray — element-wise math beats NumPy
- **SIMD transcendentals via libmvec.** `ndarray.exp`/`sin`/`cos`/`log`/`tan`/`sqrt`/`cbrt`
  for contiguous `double` arrays route through an isolated kernel TU (`ufunc_simd.cpp`)
  compiled `-fveclib=libmvec -fno-math-errno`, so they vectorize through glibc's vector
  math — **without** `-ffast-math`, so results stay strictly IEEE and the rest of cheatah's
  arithmetic is untouched. `exp` now wins ≈3×, `sin` ≈5× at 16384 elements.
- **`array ⊕ scalar` broadcasting fast path.** It was doing a bounds-checked `at()` per
  element (no SIMD); a contiguous fast path took `ndarray.add` from **≈20× slower than
  NumPy to ~even**, speeding up every scalar-broadcast op.
- **`purrc` now passes `-fno-math-errno`** (lets `sqrt`/algebraic math vectorize; strictly
  IEEE, unlike `-ffast-math`) and links `-lm` for the libmvec symbols.

### Docs, benchmarks & tooling
- **Honest, comprehensive vs-NumPy comparison** on the performance page: ~25 routines
  across dimensions, full operand shapes stated (a 2×2 matrix, a 16384-element vector,
  n×n ⊗ n×n), green/red speedup styling, and the NumPy version compared against.
- `scripts/numpy_compare.py` expanded to the whole library and made **fair** (full-SVD vs
  full-SVD, values-vs-values); new `scripts/linalg_sweep.py` finds per-function crossovers.
- Doc accuracy pass: `@alloc` rows now match the code (zero-copy where it is), algorithm
  names updated (Golub–Reinsch, tridiagonal QL), test names highlighted in the rendered
  tables.

### Fixes
- Corrected stale `@alloc`/behavior annotations found in an audit (e.g. `dot`'s "2-D
  matmul" comment — it rejects non-vector 2-D input; complex `dot`/`vdot` scratch claims).

## v0.5.0-alpha (2026-06-08) — performance, honestly: @perf everywhere, vs-CPython & vs-NumPy, and a smarter editor

This release is about **measuring** cheatah honestly and surfacing those numbers
where you work. Every standard-library function now carries a measured **Performance**
row; the benchmarks compare against both interpreted CPython and NumPy/LAPACK (and
report where cheatah *loses*, not just where it wins); the numeric core gains
element-wise math; and the VS Code extension becomes a real reference tool.

### Performance & benchmarks
- **`@perf` on every function.** The reference docs and editor hover now show a measured
  *Performance* row per function — cheatah ns/call vs the honest baseline (CPython, or
  **NumPy** for the numeric modules) with the version it was measured against. Numbers
  live in one provenance-tagged `docs/perf_data.json`, regenerated periodically by
  `scripts/perf_suite.py` (NOT in the QA gate — benchmarks are noisy/machine-specific).
- **Honest benchmark methodology.** Benchmarks are **elision-proof** (vary input +
  accumulate + print, so the optimizer can't delete the work — a naive loop measured a
  bogus "125000×"). cheatah is ~**20–35×** faster than CPython on real loops, ~**1×**
  where the work is already native (`hashlib`), and the whole-program suite (Mandelbrot,
  N-body, RK4, integral) runs **14–97×** faster.
- **cheatah vs NumPy, by dimension.** cheatah *wins* small/medium dense `matmul`/`solve`/
  `det`/`inv` and small `eigvalsh` (the few-level-Hamiltonian physics case) by avoiding
  Python/dispatch overhead; NumPy's BLAS/LAPACK win at scale and on `dot`/large `eigvalsh`.
  Reported both ways. (Large-dimension speedups are the future **cheatah-gpu** story.)
- **No garbage collector** — memory safety is RAII scopes + `shared_ptr` refcounting, so
  there are no GC pauses; documented on the performance page.
- The benchmark harnesses (`app_compare`, `perf_compare`, `numpy_compare`) are themselves
  **rewritten in pure cheatah** (dogfooding); the QA gate stays in trusted tooling.

### Numeric core (`ndarray`)
- **Element-wise math ufuncs** — `sqrt`/`cbrt`/`exp`/`log`/`sin`/`cos`/`tan`/`abs` over a
  whole array (the array forms of the scalar `math` module; ≈ NumPy ufuncs),
  SIMD-vectorized on the contiguous fast path.

### VS Code extension
- **Richer hover** — each function shows a divided facts block: **Performance** (@perf),
  **Complexity**, **Allocation**, and the **tests** that cover it, with icons.
- **Go to Definition (Ctrl-click)** on a stdlib call or an imported module opens its C++
  header — resolved against the cheatah **runtime you pick** (`cheatah.root` setting),
  the workspace, or headers **bundled with the extension** (kept in sync with the built
  runtime).
- **User structs & interfaces** — hover a type for its definition (fields/methods/
  interfaces), a method/field for its doc; Ctrl-click jumps to the declaration.
- **Module names are colored**, and the QA gate now **auto-reinstalls the extension**
  from the freshly-built runtime, so the editor never drifts (it had been stuck on
  v0.2.0). C++ IntelliSense for the benchmark sources fixed.

### Docs
- The performance page leads with the three benchmark comparisons grouped together.
- Audited every doc for accuracy and fixed broken/again-runnable examples across the
  guides and module READMEs.

## v0.4.0-alpha (2026-06-08) — complex linear algebra

cheatah becomes a tool for **complex** linear algebra — the kind physics (quantum,
plasma) and signal processing actually need. The numeric core can now store and
operate on complex numbers, the `linalg` module gains complex inner-product spaces
and Hermitian eigensolvers, and a real matrix finally yields the **complex
eigenvalues it mathematically has** instead of throwing. The docs site grows three
new guides (Getting Started, Coming from Python, Security).

### Numeric core (`ndarray`)
- **Complex element type.** The array element constraint widened from `Numeric` to
  **`Field`** — a real arithmetic type *or* a `std::complex` of a floating type — so
  complex matrices and vectors are first-class. Complex arrays print Python-style
  (`a+bj` / `a-bj`).
- **Construct & inspect complex arrays:** `complex(re, im)`, `real(a)`, `imag(a)`,
  `conj(a)`.
- `io.print` renders complex scalars Python-style too (`8+3j`, not `(8,3)`).

### Linear algebra (`linalg`)
- **Complex spectra.** `eig` / `eigvals` on a general real matrix now return the
  **complex** eigenvalues (a rotation gives ±i) *and* complex eigenvectors (inverse
  iteration) — they no longer throw on a complex conjugate pair. `eigh` / `eigvalsh`
  return the guaranteed-real spectrum (the numpy split).
- **Complex Hermitian eigensolver.** `eigh` / `eigvalsh` accept a complex Hermitian
  matrix → real eigenvalues, complex eigenvectors (the quantum-mechanics workhorse),
  via a real symmetric 2n embedding.
- **Complex inner-product spaces:** complex `dot` (bilinear) and `vdot`
  (conjugate-linear Hermitian inner product), complex `matmul`, and `conj_transpose`
  (the Hermitian adjoint Aᴴ).

### Docs
- New site guides: **Getting Started** (with `.purr` → `.so` compile + static/dynamic
  link diagrams), **Coming from Python** (porting guide), and **Security** (built-in
  protections vs. what you still own).
- **Syntax highlighting** in the generated site's code blocks — a lightweight,
  theme-matched highlighter that reuses the compiler's own keyword set.
- Fixed code-block rendering in the generated site (spaces inside `<pre>` were being
  collapsed). `compiler/PYTHON.md` now documents struct **methods** and
  **interfaces**, not just data classes.

### Not yet
- Templating the real decomposition routines over a generic `FloatingPoint` element
  type (float32) is deferred to a follow-up — float arrays aren't constructible from
  cheatah source yet.

## v0.3.0-alpha (2026-06-07) — a real language: methods, interfaces, generic numerics, and IntelliSense

Third pre-alpha, and the largest yet. This release turns cheatah from a small
scripting core into a **statically-typed, concept-driven language**: structs gain
methods and interfaces (lowered to C++ concepts), the numeric core becomes
**generic over its element type**, and the editor gets full **IntelliSense**. A
standing rule lands too — *every* template is concept-constrained, so misuse yields
a named error, never template spam.

### Language
- **Control flow:** `break`, `continue`, `elif`, and `match`/`case`.
- **Collections:** growable lists (`xs.append(v)` / `append(xs, v)`), index
  assignment (`d[k] = v`, `xs[i] = v`), and empty typed declarations
  (`let xs: list[int] = []`).
- **Slicing & indexing:** `a[i:j]` (and `a[i:]`, `a[:j]`), **negative indices**, and
  Python-style string indexing (`s[i]` is a length-1 string, so `s[i] == "<"`).
- **Method-call syntax:** `obj.method(...)` (UFCS) — including string predicates
  `s.startswith/endswith/contains`.
- **Struct methods:** declare `fn method(self, …)` in a struct body → a real C++
  member function (`self` is implicit; non-mutating methods are emitted `const`).
- **Interfaces:** `interface Shape { fn area(self) … }` lowers to a **C++20 concept**;
  `struct Circle : Shape { … }` adds a compile-time `static_assert` (a struct that
  doesn't fulfill it fails with *"Circle must fulfill Shape"*, not a template dump);
  and `fn describe(s: Shape)` becomes a concept-constrained `auto` — static
  polymorphism, no inheritance, no vtables.

### Numerics — generic over the element type
- **`ndarray` is now `basic_ndarray<T>`** over any `Numeric` element type, **deduced
  from the literals** (`array([1,2,3])` is integer, `array([1.0,…])` is double).
  `NDArray` remains the default `basic_ndarray<double>`, so existing code is unchanged.
- **Declarative SIMD:** element-wise ops use `std::transform(std::execution::unseq, …)`
  and `sum` uses `std::reduce(unseq)` — vectorized for any `T` — with a correct
  C-order fallback for broadcast/strided views. The linalg SIMD model (pure
  auto-vectorization, and the no-SIMD behavior) is now documented in `simd.hpp`.
- A `Numeric` / `FloatingPoint` concept split is in place for the linalg
  generalization to come.

### io
- **`Printable` protocol:** `io.print` / `io.str` now render **lists, dicts,
  structs, and ndarrays** — a value is printable if it streams, exposes a `str()`
  method, or is a container of printables (recursive). `NDArray` gained a `str()`.

### Performance
- **Automatic string-concatenation optimization:** the compiler rewrites
  `x = x + a + b` into in-place appends (`x += a; x += b`), turning O(n²)
  string-building into O(n) with no full-length temporaries — an ease-of-development
  guarantee, no manual `+=`/builder needed.
- A new **[Performance](docs/performance.md)** docs page documents the
  compile-time-for-run-time bargain.

### Compiler & policy
- **Constrain-all-templates:** every emitted function/method parameter is a
  concept-constrained `auto` (the baseline `Value`), and every library template
  carries a concept (`Numeric`, `Ordered`, `Printable`, `Sized`, …). No
  unconstrained templates anywhere — comprehensible compile errors by construction.

### Tooling
- **VS Code extension → IntelliSense.** Hover any stdlib/builtin function for its
  signature, params, and docs; type `module.` for autocomplete. Backed by a
  generated `functions.json` (built from the Doxygen XML), plus a "Get Started"
  walkthrough. The extension is now versioned with the language.
- **QA gate enforces the extension stays in sync** — a new hard-gate stage
  regenerates the extension hover DB from the stdlib API and fails the push if it
  drifted, so the editor never ships stale relative to the library.
- The docs generator now renders hand-written **guide pages** (e.g. Performance).

### Quality
- **100% unit-test line + function coverage** and **100% Javadoc** maintained across
  all the new code, under the existing ASan + UBSan + Valgrind QA gate.

## v0.2.0-alpha (2026-06-07) — networking, a bespoke docs site, and a three-tier test system

Second pre-alpha. The language core is unchanged; this release adds **networking**
to the standard library, replaces the documentation pipeline with our **own
site generator**, and builds out a **three-tier, per-function test system** behind
a stricter QA gate.

### Standard library
- **New `socket` module** — a thin, memory-safe BSD-socket wrapper
  (`tcp_listen`/`tcp_connect`/`accept`/`send`/`sendall`/`recv`/`bind`/`listen`/
  `connect`/`close`/`local_port`/`last_error`, …). `import socket`.
- A **pure-cheatah docs server** (`scripts/serve-docs.purr`) written entirely in
  `.purr` on top of `socket` — no `cpp { }`, no raw pointers — that serves the
  generated site over HTTP. Proof that real programs can be written in cheatah.

### Documentation
- **Bespoke documentation site.** Doxygen is now used *only* as the C++ parser
  (it emits XML); our own generator (`docs/gen/generate.py`) renders a modern
  static site — left module sidebar, client-side symbol search, a source browser,
  a light/dark toggle, cache-busted assets, and accessible (WCAG 2.1 AA) contrast.
  Replaces doxygen-awesome entirely.
- **Structured doc tags.** The old combined `@note` is split into `@complexity`
  (Big-O) and `@alloc` (heap behavior); every function also links **three test
  kinds** — `@test` (unit), `@crtest` (compile-run), `@systest` (system) — straight
  to their source. **100% Javadoc coverage** of the public stdlib, plus a
  behavioral description for ~190 functions.

### Testing
- **Three-tier, per-function tests:** a C++ **unit** test and a **compile-run**
  test (compile a `.purr` calling the function, run it on the runtime, assert exact
  stdout) for every function; a **comprehensive per-module system** test that
  exercises *every* function of its module; and six **cross-module system apps**
  (GradeReport, LinearSolve, EventLog, Integrity, MonteCarlo, NetworkRoundtrip)
  that only pass if many modules cooperate.

### Quality & security
- The QA gate now **hard-fails** below **100% unit-test line+function coverage** and
  below **100% Javadoc coverage**. ASan/UBSan + Valgrind run across all test tiers.
- The ~200-test per-function compile-run battery is **opt-in** (`QA_GATE_FULL_CR=1`)
  so the default gate stays fast.
- On a passing push to `main`, the docs site is **auto-regenerated**.

## v0.1.0-prealpha (2026-06-06) — first pre-alpha

The first tagged pre-alpha of the cheatah language: it compiles and runs, with a
standard library, an editor extension, and CI that runs every test under two memory
checkers.

### Language
- Python-like surface, C-style `{ }` blocks, compiles to native code via `purrc`
  (lexer → parser → codegen → C++), run by the headless `cheatah` runtime.
- `let` variables; `int`/`float`/`str`/`bool`; full operators incl. `**` (power);
  `if`/`else if`/`else`, `while`, `for … in range(…)`; `fn` functions (recursion);
  `struct` records; `list`/`dict`/`array` collections; `try`/`except` + `raise`;
  `import` with `as` aliases and dotted modules.
- **`cpp { … }` raw-C++ escape hatch** — file scope at the top level, inline inside
  a function (memory safety is the author's responsibility there).
- **`;`** is an optional statement separator/terminator (and struct-field separator).

### Standard library
`builtins`, `io`, `os`, `string`, `math`, `time`, `datetime`, `random`,
`statistics`, `hashlib`, and a SIMD numeric core (`ndarray` + numpy-style `linalg`).
Each module builds as both a static and shared library.

### Tooling
- VS Code extension ([editors/vscode/](editors/vscode/)): syntax highlighting
  (with embedded C++ in `cpp { … }`) and a "Seti + cheetah" file icon theme.
- `purrc --version` / `cheatah --version`.

### Quality & security
- 100+ tests (unit + purrc→runtime end-to-end). QA gate (pre-push hook) runs them
  under **ASan + UBSan** and **Valgrind**, plus release benchmarks.
- Security review + hardening (ndarray overflow/OOB guards); threat model and the
  Unix-interface/MCP safe-design plan in [SECURITY.md](SECURITY.md).

### Known limitations
- Single-trust model — do **not** run untrusted `.purr` yet (no sandbox).
- No Unix system-call interface or MCP server yet (designed, not built).
- Native GitHub `.purr` highlighting pending a github-linguist submission.
