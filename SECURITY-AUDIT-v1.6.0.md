# cheatah — Security Audit, v1.6.0-alpha

A full-surface security review performed for the v1.6.0-alpha release, covering the
whole codebase (not only the delta since v1.5.0-alpha): the numeric core, the external-
input parsers, the compiler frontend and driver, the runtime module loader, the network
stack, the crypto primitives, and the module-integrity chain. Findings below were each
reproduced or confirmed against the code and the passing test suites; every confirmed
issue is fixed in this release, and the fixes ship behind the same 100%-coverage +
ASan/UBSan/TSan/Valgrind gate as the rest of the library.

## Threat model

Unchanged from [SECURITY.md](SECURITY.md): cheatah is **single-trust today** — you run
your own `.purr`, so the language surface is not an attack boundary. The security-
relevant boundaries are the ones that ingest **external data or code**: the network
stack (a hostile/MITM server), the parsers (untrusted JSON/XML/HTML/regex — the
scraper/dataset path), the crypto + module-integrity chain (tampered modules,
side-channel observers), and — forward-looking — the `biome` package manager, which
will feed purrc **third-party module headers** and is the point at which several
"inert today" findings become live. Fixes are prioritized accordingly.

## Prior fixes — confirmed intact, no regression

The v1.3.0/v1.5.0 hardening was re-verified present and correct (with its regression
tests green): TLS full X.509 chain + hostname/SAN + expiry validation on by default
(multi-SAN iterated; P-384/SHA-384 chains); WebSocket frame-bounds (the previous OOB
read); `requests` body/Content-Length/chunk caps + cross-host credential stripping;
P-256/P-384 on-curve validation; RSA `e=1`/even rejection; constant-time TLS-Finished
and AEAD-tag compares; fail-closed module integrity (SHA-512 + Ed25519, TOCTOU-closed
via `/proc/self/fd`).

## Findings and the fixes shipped in v1.6.0-alpha

| # | Severity | Area | Finding | Fix |
|---|---|---|---|---|
| 1 | **High** | `linalg` `matrix_power` | `result(r*r)` was sized before any overflow-checked path; a `{2^32,2^32}` broadcast view (a few bytes) wrapped `r*r`→0 → heap **OOB write** (ASan-confirmed SEGV) | Size via the checked `ndarray::detail::product({r,r})` — throws on wrap |
| 2 | **High** | `linalg` `kron` | the front + kernel pre-multiplied `{m*p, k*q}` outside `product()`'s check → wrap → under-alloc → **OOB write** in `kron_kernel` | Overflow-check each output dim via `product({x,y})` before it collapses into the shape |
| 3 | **High** | `parsers.json` | the default owning `Document` is a recursively-nested `vector<Node>`; its destructor overflowed the stack on valid deep input (`[[[…]]]`, ~1 MB → SIGSEGV, every build mode) | Cap container nesting at 1000 during the validated parse (bounds the tree → the destructor); trusted `Validate=false` unaffected |
| 4 | **High** | `parsers.xml` | `text()`/`collect_text` recursed per child level → stack overflow on a deep `<a><a>…` chain | Rewrote iteratively (explicit stack, order preserved) — no depth limit, no recursion |
| 5 | **Medium** | `requests` | `dechunk` + the header loop were O(n²) (each iteration sliced-copied the remaining body/header block); a many-tiny-chunks response under the byte cap → multi-minute–hours CPU hang (remote DoS, reachable on any attacker-influenced fetch) | Added `string.find(s,sub,start)` and rewrote both loops to walk an absolute offset — O(n), no tail-copy |
| 6 | **Medium** | `regex` | crafted PATTERN could overflow the parser's recursion (`(((…`), the epsilon-closure recursion (`a?`×N), or exhaust memory via the unbounded lazy-DFA cache (`a` + ~30 `.` → 2^30 states) | Parser depth cap (1000, clean error); iterative closure; 100k-state DFA-cache ceiling that throws rather than OOMs. Match **time** stays linear |
| 7 | **Medium** | `p256`/`p384` ECDSA **signing** | the secret-scalar multiply (`k*G`, `d*G`) skipped the add on a zero window and indexed the comb table by the secret selector, and `jac_add`/`jac_double` branched on secret intermediates → local timing side-channel leaking nonce bits → private-key recovery | Branch-free constant-time comb: `jac_double_ct`, mask-selecting `jac_add_ct`, masked-scan `ct_select`, unconditional per-step add. Verify path (public data) unchanged. Differentially self-checked vs the reference on every case |
| 8 | **Medium** | `purrc` supply-chain | a module header's `// cheatah-link:` tokens were passed VERBATIM to the backend compile+link — a malicious dependency shipping `-fplugin=`/`-specs=`/`@response`/`-Wl,…` = code execution on the consumer's build host (inert under single-trust, critical under `biome`) | Allowlist genuine linker inputs only (`-l`, `-L`, `-pthread`); drop anything else with a warning |
| 9 | **Medium** | runtime trust | env-enforced strict mode (`CHEATAH_VERIFY`) could have its trust anchor SUBSTITUTED by an argv `--trust /tmp/evil.pub` (the "argv can't weaken strict" guarantee covered only turning verify off) | Reject argv `--trust`/`--trust-runtime` when strict came from the environment; configure trust via `CHEATAH_TRUST` |
| 10 | **Low** | `rsa_verify` | no upper bound on the public exponent `e` → `modexp` CPU-DoS from a malicious cert | Cap `e` at 64 bits (real exponents ≤17 bits); `e<3`/even rejection unchanged |
| 11 | **Low** | `aead` | no single-message length cap → a >~64 GiB message wraps the 32-bit block counter, reusing keystream/tag-mask within one message | Cap all six ChaCha20-Poly1305 / AES-GCM entries at 2^36 bytes (GCM's 2^32-block limit) |
| 12 | **Low** | `tls` | the server handshake flight accumulated into transcript/cert_chain pre-auth with no total bound → unbounded pre-auth memory from a hostile server | 256 KiB total-flight cap, fail closed |

## Accepted / deferred (informational — no live bug)

- **`linalg` scalar-out `T out;` and `trace`'s strided pointer read** (backend.hpp /
  routines.cpp). The host kernels **always** write `out`, and every 2-D view the public
  ndarray API can construct keeps `trace`'s diagonal index in-bounds (there is no
  slicing/transpose/negative-stride view op today), so neither is a reachable bug. They
  are one future feature (a device kernel that early-returns; a reversed-stride view)
  from mattering. Recommended hardening — value-init `T out{}` and a bounds assert in
  the trace kernel — is **deferred to the linalg owner** to land alongside the in-flight
  device-seam refactor of those files, rather than conflict with it.
- **`os.system` + unconfined filesystem mutators** and **`Validate=false` parser paths**
  are by-design under single-trust and out of scope until the sandbox/MCP model; noted
  as prerequisites for that work in [SECURITY.md](SECURITY.md).
- **Fixed `/tmp/cheatah_*` CI log/CSV paths** (dev/CI-only; not a runtime surface) — a
  symlink-hardening (`mktemp`) is a low-priority follow-up for shared runners.

## Evidence

Every fix keeps its module's suite green and adds coverage for the new path: the
`linalg` overflow throws under ASan where it previously SEGV'd; the JSON/XML repros
that crashed now error/return cleanly; `requests`/`string` (131), `regex` (14),
crypto+TLS (139) suites pass; the constant-time ECDSA is verified by the RFC 6979
deterministic KATs (exact signatures), the TLS ECDSA-P256/P384 handshakes vs
`openssl s_server`, and a new differential self-check
(`CheatahP256.ConstantTimePointOpsMatchReference`) that compares the CT ops to the
branchy reference on the general case and every special case. The full release gate
(100% line+function coverage, ASan/UBSan, TSan, Valgrind, cppcheck) runs over all of it.

## Residual and non-goals

Timing constant-ness is verified structurally (no secret-dependent branch/index), not
by a statistical timing harness. The single-trust model still means a program that
`import os` has the host's authority by design; the capability/sandbox layer that
mediates that (and the `biome` untrusted-dependency model that makes findings 8–9 live)
is future work. No OCSP/CRL revocation. See [SECURITY.md](SECURITY.md) for the roadmap.
