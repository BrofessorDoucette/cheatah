# Security

cheatah is **alpha**. This document states the current threat model, records
the standing security review, and lays out *how* we intend to safely grow a Unix
interface and (eventually) an MCP server that lets an LLM drive cheatah — **without
introducing arbitrary-code-execution holes.** No MCP server capabilities exist yet.

## Reporting

Please report suspected vulnerabilities privately to the maintainer rather than
opening a public issue.

---

## Current threat model (today)

cheatah is **single-trust by default**, like Python or a C++ compiler — there is no
sandbox, so any module you run has your full privileges:

> By default, the `.purr` source and the compiled `.so` you run are **fully trusted**.
> The person who writes/compiles/runs the program is whose machine it runs on.

Since **v1.1.0**, modules can be **signed** (`purrc --sign`, with from-scratch Ed25519)
and the runtime can **verify** them before loading (`CHEATAH_VERIFY=strict`), so trust can
be delegated to a signer you pin rather than auditing every byte — see
[docs/security.md](docs/security.md). That is *authenticity and integrity*, **not** a
sandbox: a signed module still runs with full privileges, so you must trust the signer.

Under the default (unsigned) model these are **features, not bugs**:

- <b>`os.system`, raw filesystem access, process/env access</b> — Python-parity
  conveniences for your own scripts.
- <b>The `cpp { … }` escape hatch</b> — runs arbitrary C++. Memory safety is the
  author's responsibility inside it (see [README](README.md#the-language)).
- **No sandbox** — a cheatah program can do anything its user can.

What we still hold ourselves to *even under full trust* — because bugs are bugs:

- **Generated code is memory-safe** by construction (value types and STL containers;
  no raw `new`/`delete`, no manual pointer arithmetic) — except inside `cpp { … }`.
- **No leaked memory or OS handles.** Heap objects free by scope; every stateful resource
  (file, socket, TLS/WebSocket connection) is an owning guard value, and `with … { … }`
  closes it on every exit path. Pure cheatah code therefore leaks neither memory nor a
  descriptor/connection — the sole in-language leak is the explicit, opt-in raw-handle API
  (close it yourself, as with Python's `open()` without `with`).
- **No command injection in the toolchain.** `purrc` invokes the C++ backend with
  `fork` + `execvp` (no shell), so paths can never be reinterpreted as shell syntax.
- <b>The runtime validates a module before `dlopen`</b> (`runtime/main.cpp`):
  canonicalizes the path, requires a regular file, refuses world-writable modules,
  and checks the ELF magic.
- **Optional integrity verification before load** (`runtime/integrity.cpp`): a
  **SHA-512** checksum (corruption) and an Ed25519 signature (tampering), plus a
  build-runtime compatibility check — opt-in, fail-closed, and fd-bound to the bytes
  it verified. Module integrity uses a **512-bit digest throughout**; SHA-256 remains in
  the `hashlib` stdlib module for applications but is not used for signing.
- **Standard-library bounds/overflow checks** so malformed inputs raise instead of
  corrupting memory (see the review below).
- **The QA gate runs the whole suite under ASan + UBSan *and* Valgrind**
  (`security/run-valgrind.sh`), with 100% stdlib line+function coverage — memory
  errors, UB, and leaks fail the build. Coverage is measured across the in-process unit
  tests **and** the system tests that drive a module against a real peer (the `tls`/
  `websocket` clients against `openssl s_server` / a Node `ws` server), so the crypto and
  network modules are held to the same 100% bar as the rest — tested against real
  implementations, never a mirrored copy of the protocol.

Integrity verification is paid **once at load**, never during execution, and is
**zero-overhead** when off (the module isn't even read). Per-tier cost on `x86_64`
([`tests/benchmarks/integrity_bench.cpp`](tests/benchmarks/integrity_bench.cpp)):

| Tier enabled | 64 KiB module | 1 MiB module |
|--------------|--------------:|-------------:|
| off (default) | ~1.6 µs | ~1.6 µs |
| + SHA-512 checksum | ~0.16 ms | ~2.8 ms |
| + Ed25519 signature (strict) | ~2.4 ms | ~7.4 ms |
| + signed runtime manifest | ~4.4 ms | ~9.2 ms |

The SHA-512 pass scales with module size; each Ed25519 verify is a roughly fixed ~2 ms
(the crypto is from-scratch and audit-oriented, not throughput-tuned). See
[docs/security.md](docs/security.md) for the full breakdown.

## Standing security review

| Area | Finding | Status |
|------|---------|--------|
| `ndarray` shape math | `product()` could overflow `size_t` → under-allocation → OOB writes | **Fixed** — overflow-checked, throws |
| `ndarray` dims/indices | negative `long long` → huge `size_t`; no index bounds check (OOB read) | **Fixed** — rejects negatives, bounds-checks `at()` |
| `purrc` backend invocation | shell metacharacter / command injection | **Safe** — `fork`+`execvp`, no shell |
| runtime module load | loading a wrong/tampered `.so` | **Mitigated** — canonical path + regular-file + world-writable + ELF-magic checks; plus **optional** SHA-512 + Ed25519 verification before load (`CHEATAH_VERIFY=strict`). The path-sniff checks have a small TOCTOU window; the integrity verification does **not** — it hashes and loads the same open fd (`/proc/self/fd`). |
| module authenticity | running a substituted/untrusted-signer module | **Mitigated (opt-in)** — strict mode requires a valid Ed25519 signature from a pinned trusted key, fail-closed and non-downgradable. Not a defense if the attacker can rewrite the runtime or the trust file. |
| `cpp { … }` escape hatch | arbitrary C++ / no memory safety | **By design** under full trust; **disabled** in the sandboxed mode proposed below |
| `os.system` / raw fs / env | arbitrary host effects | **By design** under full trust; **capability-gated** in sandboxed mode |
| `websocket` frame parsing | a 64-bit payload length overflowed `header + len` → **out-of-bounds write** during unmasking; unbounded frame/message reassembly → OOM; control-frame rules (≤125, not fragmented) unenforced | **Fixed** — per-frame + per-message size caps (default 64 MiB) checked before any size math/copy; RFC 6455 control-frame, reserved-bit, opcode, and fragmentation-state validation. Regression tests drive `recv()` with hostile frames. |
| `requests` response handling | unbounded response body → OOM; a malformed/overflowing/negative `Content-Length` (and non-numeric status) threw out of the "never raises" path → **crash**; chunk-size integer overflow | **Fixed** — `Options.max_bytes` cap (default 100 MiB) on the body; safe `parse_uint` for status/Content-Length (sets `error`, never throws); chunk-size overflow guard. |
| `requests` cross-host redirect | Basic-auth / `Authorization` / `Cookie` re-sent to the redirect target on a **different host** → credential leak | **Fixed** — credentials and cookies stripped on a host change; the request runs on a private copy so the caller's `Options` is not mutated. |
| `tls` **server authentication** | previously verified leaf-key possession (CertificateVerify) but did **no** X.509 chain / hostname / expiry validation → an active **MITM** with any certificate completed the handshake | **Fixed** — from-scratch X.509 path validation ([stdlib/tls/x509.hpp](stdlib/tls/x509.hpp)): chain to the system CA store, `subjectAltName` hostname match (RFC 6125 wildcards), and validity-period check, **on by default** (RSA-PKCS1-SHA256 / ECDSA-P256-SHA256 / Ed25519 chain sigs; other algorithms fail closed). So `https://` / `wss://` (and `requests`/`websocket`) resist an active MITM. Opt out per-connection with `insecure` (pinned peer) or `ca_file` (private CA). Not yet: P-384 / SHA-384 chain sigs, OCSP/CRL revocation. |
| `p256` ECDSA verify | an off-curve / invalid-curve public key (coords in range) entered the group law without a curve-equation check | **Fixed** — `on_curve()` validates `y² = x³−3x+b` before use (SP 800-56A point validation). |
| `rsa_verify` (PSS) | a public key with exponent `e = 1` (or even `e`) was accepted → trivial signature forgery | **Fixed** — reject `e < 3` and even `e` before any modexp. |
| `tls` Finished MAC compare | non-constant-time `memcmp` of the handshake MAC | **Fixed** — constant-time compare (parity with the AEAD tag check). |
| `p256`/`p384` ECDSA **signing** | the secret-scalar comb skipped the add on a zero window, indexed the table by the secret selector, and used branchy point adds → local timing side-channel leaking nonce bits (→ private key) | **Fixed (v1.6.0)** — branch-free constant-time comb (`jac_double_ct`/`jac_add_ct`/`ct_select`: masked selects, unconditional per-step add, no secret-indexed access) for both curves; differentially self-checked vs the reference on every case. Verify paths (public data) unchanged. |
| `linalg` `matrix_power`/`kron` | a product of two dims was formed as raw `size_t` before `product()`'s overflow check → wrap → under-alloc → **OOB write** (ASan-confirmed for `matrix_power` via a broadcast view) | **Fixed (v1.6.0)** — each product-of-dims routed through the checked `detail::product({x,y})`, which throws on wrap. |
| `parsers.json`/`parsers.xml` | valid deep input overflowed the C++ stack — the JSON owning-`Document` **destructor** (recursive `vector<Node>`) and XML `text()`'s recursive walk (~1 MB → SIGSEGV, every build mode) | **Fixed (v1.6.0)** — JSON caps container nesting at 1000 during the validated parse; XML `collect_text` rewritten iteratively. |
| `requests` chunked/header decode | O(n²) — each iteration sliced-copied the remaining body/header block; a many-tiny-chunks response (under the byte cap) → CPU-exhaustion DoS on any attacker-influenced fetch | **Fixed (v1.6.0)** — added `string.find(s,sub,start)` and rewrote both loops to walk an absolute offset (O(n), no tail-copy). |
| `regex` crafted pattern | parser recursion (`(((…`), epsilon-closure recursion (`a?`×N), and an unbounded lazy-DFA cache (`a`+~30`.`→2^30 states) were stack/memory-exhaustion DoS (match **time** was already linear) | **Fixed (v1.6.0)** — parser depth cap (clean error), iterative closure, 100k-state DFA-cache ceiling that throws rather than OOMs. |
| `regex` pattern length | `compile()` spends ~40 bytes of NFA program per pattern byte, so a crafted multi-megabyte pattern was a ~40× memory-amplification DoS the depth cap (depth, not breadth) and state budget (match-time, not compile-time) never covered | **Fixed (v1.10.0)** — 64 KiB pattern-length cap, rejected as a malformed pattern (`"pattern too long"`), checked before any allocation. |
| `regex` v1.10 matcher rewrite | new hostile-input machinery — byte-offset transition encoding, hoisted table pointers across cache growth, SWAR same-byte-run skips, self-transition LUT, adaptive literal probe, reversed-program `$` pass | **Safe** — audited for bounds/overflow (max table offset ≈103 MB < `INT_MAX`), stale-pointer windows (every hoisted pointer refreshed after growth), and skip soundness (skips fire only on proven self-transitions of non-accepting states); evidence: ASan/UBSan/TSan/Valgrind clean over the suite, plus a differential harness agreeing with RE2-as-oracle on thousands of generated inputs including all 256 byte values. |
| `purrc` `// cheatah-link:` | module-header link tokens passed VERBATIM to the backend compile+link → `-fplugin=`/`-specs=`/`@response`/`-Wl,…` = build-host code execution (inert under single-trust; critical once `biome` feeds third-party headers) | **Fixed (v1.6.0)** — allowlist genuine linker inputs only (`-l`/`-L`/`-pthread`); anything else dropped with a warning. |
| runtime trust anchor | env-enforced strict mode's trust anchor could be **substituted** by an argv `--trust /tmp/evil.pub` | **Fixed (v1.6.0)** — argv `--trust`/`--trust-runtime` rejected when strict came from the environment (configure via `CHEATAH_TRUST`). |
| `rsa_verify` exponent / `aead` length / `tls` flight | unbounded RSA `e` (modexp CPU-DoS); no AEAD single-message cap (32-bit block-counter wrap → keystream reuse); unbounded pre-auth TLS handshake flight | **Fixed (v1.6.0)** — `e`≤64 bits; AEAD messages capped at 2^36 B; TLS server flight capped at 256 KiB. |
| portable (no-hardware) AES / GHASH | software S-box lookups and a bit-serial GHASH branch are secret-dependent → cache-timing leak | **Documented limitation** — the **hardware** AES-NI / PMULL path (default on x86-64 / ARMv8-crypto) is constant-time; the scalar fallback (rare hosts / forced-portable) is correctness-first, not timing-hardened. |

ASan + UBSan + Valgrind: **all tests pass clean.** The `ndarray` issues were latent (no
test reached them) and were found by manual review, not the sanitizers — a reminder
that sanitizers only cover exercised paths.

---

## The hard part: a Unix interface, and later an LLM (MCP) driving cheatah

We want two things that are in tension with the single-trust model:

1. **A real Unix system interface** — in the spirit of K&R's "The UNIX System
   Interface": file descriptors (`open`/`read`/`write`/`close`/`lseek`), `stat`,
   directory iteration, processes, pipes, signals, time.
2. **Eventually, an MCP server** so an assistant (e.g. Claude) can generate and run
   cheatah programs.

(2) **breaks the trust assumption**: LLM-generated `.purr` is *not* author-trusted.
It can be buggy or steered by prompt injection. Running it with the full Unix
interface + `os.system` + `cpp { … }` is arbitrary remote code execution. So the
rule is:

> **Trust is decided by the HOST, never by the program.** A program cannot widen
> its own privileges. LLM/remote-supplied code is *always* run sandboxed.

### Design: trust tiers + capabilities + OS sandbox (defense in depth)

**1. Two execution modes, set by the host.**
- **Trusted** (a human running their own code): full Unix interface, `os.system`,
  `cpp { … }` — today's behavior.
- **Sandboxed** (any code the operator didn't author — MCP/LLM output, untrusted
  input): **default-deny**. Only a curated, side-effect-free subset of the stdlib.

**2. Capability-gated stdlib, enforced at *link* time.**
cheatah already links *only the modules a program imports*. We make the sandbox
profile the authority on *which modules `purrc` is even allowed to link*:
- Sandboxed builds link only safe modules (`math`, `string`, `statistics`,
  `ndarray`, `linalg`, pure `datetime`, seeded `random`, `hashlib`, `ed25519`, and a
  **capability-mediated** `io`/`fs`).
- Dangerous capabilities (`proc:spawn`/`os.system`, `net:*`, unrestricted `fs:*`,
  `env:*`) are **not linked at all** unless the host grants them — so they're absent
  from the binary, not merely unused.
- Granted capabilities are **scoped** (e.g. `fs:read=/srv/data`, no write, no exec).

<b>3. The `cpp { … }` escape hatch is refused in sandboxed mode.</b>
Raw C++ is arbitrary code. `purrc` already models the block as a single `RawCpp`
AST node, so a sandbox flag (`--no-escape-hatch`) makes the compiler **reject** any
`cpp { … }` — untrusted programs simply cannot reach raw C++.

**4. OS-level sandbox as the backstop.** Native code can still issue syscalls
(stdlib bug, or a future hole), so untrusted modules run confined:
- A **separate, short-lived child process** (the runtime forks; the child
  `dlopen`s + runs the module; results return over a pipe) so a compromise is
  contained.
- **seccomp-bpf** syscall allowlist (no `execve`, no raw `socket`, no `ptrace`, …).
- **Landlock** / mount + pid + net **namespaces** for filesystem and network
  confinement.
- <b>`rlimit`s</b> (CPU, address space, file size, no core dumps) and a wall-clock
  **timeout**.

**5. Keep the sandbox memory-safe** (so it can't be escaped via UB): generated code
stays memory-safe (no `cpp { … }` in the sandbox), stdlib stays bounds-checked,
CI keeps running ASan+UBSan, and untrusted modules can additionally be built with
`-fstack-protector-strong -D_FORTIFY_SOURCE=2` (and optionally CFI/sanitizers).

**6. The MCP boundary (future — deliberately not built yet).** When MCP lands, the
MCP server is the host and treats *all* LLM output as untrusted data: it compiles
with the sandbox profile (no escape hatch, minimal modules), runs in the confined
child with rlimits + timeout, exposes only structured/audited tools (never a raw
shell), and grants capabilities explicitly and minimally.

### What this means for building the Unix interface *now*

We can add the K&R-style Unix interface in **trusted mode first**, provided every
new primitive is designed from day one to be **capability-gated and refusable**:

- Route each syscall-backed feature through a capability (`fs`, `proc`, `net`,
  `env`, `time`) rather than a global free function, so the sandbox can withhold it
  later **without retrofitting**.
- Default to the **least-privilege** form (e.g. open a path relative to a
  capability-supplied directory handle, not an absolute path).
- Treat the escape hatch and `os.system` as **trusted-only** from the start.

Designing it this way means the eventual sandbox/MCP work is a matter of *flipping
defaults and adding the OS confinement*, not rewriting the standard library.

> **Status:** trust tiers, capability gating, the OS sandbox, and the MCP server
> are **design intent, not implemented**. Today cheatah is single-trust. Do not run
> untrusted `.purr` until the sandboxed mode above exists.
