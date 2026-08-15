# Security {#security}

<div class="cheetah-slogan">🐱 <em>Memory-safe by construction; honest about the rest.</em> 🐆</div>

The **user's-eye view** of cheatah security: what the language and toolchain
protect you from automatically, and what you are still on the hook for **today**.
For the full threat model, the standing security review, and the sandbox/MCP
roadmap, see
[`SECURITY.md`](https://github.com/BrofessorDoucette/cheatah/blob/main/SECURITY.md).

## The one assumption to internalize

cheatah has **no sandbox today**: any module you load runs with **your full
privileges** — nothing confines what a `.purr`/`.so` can do, exactly like a CPython
script or a C++ program you compile and run.

So the rule is: **run only code you trust.** That means code you wrote or audited — or,
with strict verification enabled, code **signed by a key you trust** (see *Verifying a
binary before it loads*). Code signing proves a module is **authentically from that
signer, and unmodified**; it does **not** prove the code is *safe*, and it is **not** a
sandbox. It moves the trust decision from auditing every byte to trusting whose key
signed it — it does not remove the need to trust. Everything below assumes that.

## Protections built into the language

These you get for free — the compiler and runtime enforce them so you don't have to:

- **Memory-safe generated code by construction.** Codegen emits value types and STL
  containers (`std::string`, `std::vector`, `std::unordered_map`, …) — <b>no raw
  `new`/`delete`, no manual pointer arithmetic</b> — so use-after-free and double-free are
  off the table for compiler-written code. (The one exception is the `cpp { … }` escape
  hatch — see below.)
- **Deterministic resource cleanup — no leaked heap memory or OS handles.** Heap objects are
  value types that free by scope, and every *stateful* resource (a file, socket, TLS or
  WebSocket connection) is handed out as an **owning guard value** whose destructor
  releases it — `io.File`, `socket.Conn`/`Listener`, `tls.Conn`, `websocket.Client`. A
  <b>`with resource [as name] { … }`</b> block binds one for the block and closes it on
  **every** exit path — a `return`, a `break`, or an exception. **Pure cheatah code cannot
  leak heap memory at all:** the only heap-allocating handle APIs (the flat, session-id
  forms in `tls` and `websocket`) are now **C++-only** — they live in `*_lowlevel.hpp` and
  are **not reachable from cheatah**, which sees only the owning guards. The one remaining
  in-language leak path is a `cpp { … }` block. (The `socket` fd API is still
  cheatah-visible for hand-built servers; an unclosed fd is a *resource* leak, not heap
  memory — prefer `socket.open`/`serve` guards, exactly like Python's `open()` with a
  `with`.)
- **No uninitialized values — an unset value is a bug that does not compile.** Two language
  rules close the classic uninitialized-memory hole (a top source of crashes, garbage results,
  and information-disclosure bugs):
  - A struct is built with a **C++20 designated initializer**, e.g. `Point({.x = 1})`, and any
    field you don't list is **default-initialized to a valid zero** — never left as random
    stack/heap memory. Field names are checked at compile time, so a typo'd `.field` is an
    error, not a silently-ignored value.
  - A `let` may be declared with **no value** (`let total`, or `let total: float`) because you
    may assign it later — but the compiler tracks whether it is *actually* given one. A
    variable that is never assigned is **removed**; one that is **used before it is definitely
    assigned — or assigned only on some branches (ambiguous at compile time) — does not
    compile**. An uninitialized read therefore can't reach the generated code in the first
    place.
- **Bounds- and overflow-checked standard library.** Array shape math is
  overflow-checked (a `size_t` overflow throws instead of under-allocating), negative
  dimensions/indices are rejected, and element access is bounds-checked — malformed
  inputs **raise an error, they don't corrupt memory**.
- **No command injection in the toolchain.** `purrc` invokes the C++ backend with
  `fork` + `execvp` — **never a shell** — so a crafted path can never be reinterpreted
  as shell syntax.
- **Validated module loading.** Before `dlopen`-ing a module the runtime canonicalizes
  the path, requires a **regular file**, **refuses world-writable** modules, and checks
  the **ELF magic** — so a stray or tampered `.so` is rejected, not blindly loaded.
- **Optional integrity verification of the module.** Before loading, the runtime can
  check a **SHA-512 checksum** (corruption), an **Ed25519 signature** (tampering), and a
  **build-runtime manifest** (an incompatible C runtime) — see *Verifying a binary before
  it loads* below. Off by default, so it costs nothing unless you opt in.
- **Every template is concept-constrained.** Misuse surfaces as an **early, named
  compile error** ("`Point` does not satisfy `Printable`"), not undefined behavior or
  a pages-long instantiation backtrace.
- **A QA gate that runs on every push** (a local pre-push hook), under
  **AddressSanitizer + UndefinedBehaviorSanitizer** *and* **Valgrind**, with **100%
  line and function coverage of the standard library** enforced — memory errors, UB,
  and leaks fail the build before they ship. (Caveat: sanitizers only cover *exercised*
  paths; the historical `ndarray` overflow bugs were caught by manual review, then
  fixed and regression-tested.)

## Verifying a binary before it loads

A compiled cheatah module is native code, and loading it runs that code. cheatah can
verify it first — refusing an injected, corrupted, or incompatible binary instead of
executing it — in **three independent layers**, all **opt-in** (zero cost when unused) and
all backed by crypto **implemented from scratch in the standard library** (`hashlib`,
`ed25519`), no external dependency.

### First, the key model — the same as RSA, just smaller and faster

If you've signed anything with **RSA** or GPG, you already know the model; **Ed25519**
only swaps the underlying math. Signing is **asymmetric** — you hold a **keypair**:

- a **secret key** (what RSA calls the *private* key) — kept **offline**; it **signs**.
- a **public key** — shared freely; it **verifies**.

You sign an artifact once with the secret key. Anyone holding the public key can then
confirm the artifact came from you and hasn't changed — but **cannot forge** a new
signature, because they don't have the secret key. That's identical to RSA. Ed25519 is
simply a modern signature scheme (elliptic-curve EdDSA) that does it with **32-byte keys**
and **sub-millisecond** verification instead of RSA's multi-kilobit keys — which is why a
small runtime can ship its own implementation.

`purrc --keygen <name>` writes the pair (and prints the public key):

- <b>`<name>.key`</b> — the **secret** key, created mode `0600`. Guard it like an SSH private
  key; anyone who has it can sign **as you**.
- <b>`<name>.pub`</b> — the **public** key. Hand it to whoever runs your modules; they pin it
  in a **trust file** so the runtime will accept signatures made by its matching secret key.

### The files at a glance

Compiling `app.purr` produces the module `app.so`. Each protection you turn on writes a
small **sidecar file** right next to it, which the runtime reads automatically if it's
present. The naming is consistent — <b>`X.sig` always means "the signature of `X`"</b>:

| File | Written by | What it is | Verified with | Catches |
|------|-----------|-----------|---------------|---------|
| `app.so` | `purrc … -o app.so` | the compiled module (native code) | — | — |
| `app.so.sha512` | `--checksum` | a SHA-512 **checksum** of the module (no key) | re-hash the file | accidental corruption |
| `app.so.sig` | `--sign code.key` | an Ed25519 <b>signature of `app.so`</b> | `code.pub` | tampering with the **code** |
| `app.so.rt` | `--runtime` | a text **manifest** of the build runtime (arch, glibc, libstdc++) | — (compared to the live host) | an **incompatible** host |
| `app.so.rt.sig` | `--sign-runtime rt.key` | an Ed25519 <b>signature of `app.so.rt`</b> | `rt.pub` | tampering with the **manifest** |

So there are **two separate signatures** — `app.so.sig` over the code and `app.so.rt.sig`
over the runtime manifest — made with **two different keys**. (Tier 3 explains why they're
kept distinct.) The three tiers below are just *when* each of these files is produced and
checked — and which of them you must actually **protect with filesystem permissions** (and
which you can leave wide open) is its own short section: *File permissions*, below.

### Tier 1 — `app.so.sha512`: a checksum (accidental corruption)

The `.sha512` sidecar (sha512sum-compatible) is **auto-verified whenever it's present**: if
the bytes don't match, the runtime refuses — catching disk corruption, a truncated
download, or a botched copy. No key is involved; it's a plain integrity hash. Module
integrity uses **SHA-512** (a 512-bit digest) end to end; the `hashlib` module still
exposes **SHA-256** for your own applications — it just isn't used for signing.

```sh
purrc app.purr -o app.so --checksum     # writes app.so.sha512
cheatah app.so                          # auto-checks the checksum, then runs
sha512sum -c app.so.sha512              # …and ordinary tools can check it too
```

### Tier 2 — `app.so.sig`: a code signature (deliberate tampering)

The `.sig` sidecar is the **Ed25519 signature of the module bytes**. It proves the module
was produced by the holder of the secret key and has **not changed since** — a checksum
catches accidental corruption, but only a signature stops a *deliberate* swap, because an
attacker can recompute a `.sha512` but can't forge a `.sig` without the secret key.

```sh
purrc --keygen release            # -> release.key (SECRET, mode 0600) + release.pub
purrc app.purr -o app.so --sign release.key   # writes app.so.sig (+ app.so.sha512)

# On the machine that runs it: trust the public key, and require a valid signature.
cp release.pub ~/.config/cheatah/trusted.pub
export CHEATAH_VERIFY=strict
cheatah app.so                                # refuses unless app.so.sig verifies
#   …or per run:  cheatah --verify --trust release.pub app.so
```

**Strict mode is fail-closed and non-downgradable.** A missing, invalid, non-canonical, or
untrusted signature all *refuse* the module — and there is no flag to turn verification
off, so `CHEATAH_VERIFY=strict` can't be weakened by a wrapper prepending arguments. The
runtime loads the **same file descriptor it hashed** (`/proc/self/fd`, or `/dev/fd` on
macOS), so there's no verify-then-load race; the read is size-capped.

### Tier 3 — `app.so.rt` (+ `app.so.rt.sig`): build-runtime compatibility

This tier answers a different question from Tiers 1–2: not *"is this the right code?"* but
*"was it built for <b>this</b> machine?"* A module dynamically links the host's C/C++ runtime
(glibc, libstdc++); built against a **newer** runtime than the host provides, `dlopen`
fails cryptically or misbehaves. So `--runtime` records the build environment — CPU arch,
glibc version, libstdc++ ABI — in a small **text manifest** named <b>`app.so.rt`</b> (`.rt`
for run<b>t</b>ime), and the runtime compares it to the **live host** before loading, refusing
with a readable reason.

```sh
purrc app.purr -o app.so --runtime    # writes app.so.rt  (the runtime manifest)
cheatah app.so                        # refuses e.g. "needs glibc >= 2.39, host has 2.31"
```

On its own, `app.so.rt` is plain text — anyone could edit it. To vouch for it, sign it too,
with <b>`app.so.rt.sig`</b> (literally "the `.sig` of the `.rt`"). Crucially this uses a key
**separate from the code-signing key**, so *what the code is* and *what it was built
against* are attested **independently**, and neither key can stand in for the other (e.g. a
build farm can certify the runtime without holding the key that signs releases):

```sh
purrc --keygen code-key            # the code-signing keypair  (code-key.key / code-key.pub)
purrc --keygen runtime-key         # a DISTINCT runtime keypair (runtime-key.key / .pub)

# --sign        writes app.so.sig      (code, signed with code-key)
# --sign-runtime writes app.so.rt + app.so.rt.sig  (manifest, signed with runtime-key)
purrc app.purr -o app.so --sign code-key.key --sign-runtime runtime-key.key

export CHEATAH_VERIFY=strict
export CHEATAH_TRUST=code-key.pub             # trust for the code signature (app.so.sig)
export CHEATAH_RT_TRUST=runtime-key.pub       # SEPARATE trust for the manifest (app.so.rt.sig)
cheatah app.so
```

The compatibility *check* runs whenever an `app.so.rt` is present (it prevents a crash, so
it isn't gated on strict mode); the manifest *signature* (`app.so.rt.sig`) is required only
in strict mode when a runtime trust (`CHEATAH_RT_TRUST` / `--trust-runtime`) is set.

### File permissions: what actually has to be protected

This is the part that surprises people. <b>Signing changes <em>what you must guard.</em></b> You no
longer have to protect the module's bytes from being overwritten — a tampered `app.so`
just fails verification and is refused. Instead the whole scheme reduces to a few files
whose **filesystem permissions** are the real control. Get these wrong and an attacker
walks straight through; get them right and forging a module is computationally infeasible.

| File | On whose machine | Permission that matters | If an attacker can write (or read) it |
|------|------------------|-------------------------|----------------------------------------|
| `release.key` — the **secret key** | the **signer's** | **read-only to you** — `purrc` creates it `0600`; better still, keep it **offline** | reads it → **signs as you**; every downstream check then passes for *their* malware. A leaked secret key defeats Tiers 2 **and** 3 at once. |
| the **trust file** — the `.pub` you pin (`CHEATAH_TRUST` / `CHEATAH_RT_TRUST`) | the **runner's** | **writable only by you / root** | adds **their own** public key, then signs malware with its secret half → **full bypass**. The trust file is the anchor; the runtime believes whatever keys are in it. |
| the <b>`cheatah` runtime</b> binary + its directory | the **runner's** | **writable only by you / root** | rewrites the verifier itself → it can be patched to accept anything. Nothing below the trust anchor can defend the trust anchor. |
| `app.so` — the module | the **runner's** | the runtime **refuses to load it if it is world-writable** (`o+w`) | (refused outright) — but keep it non-world-writable so that refusal isn't your *only* line, and so a local user can't stage a swap. |

And the flip side — files whose permissions you **don't** need to fret over, because they
either can't be forged or aren't a security control:

- <b>`app.so.sig` / `app.so.rt.sig`</b> — leave them world-writable if you like. A signature an
  attacker didn't produce with a **trusted secret key** simply won't verify. The hard part
  is forgery, not file access.
- <b>`app.so.sha512`</b> — anyone who can change the module can recompute its checksum, so the
  `.sha512` is only ever a **corruption** check, never a tamper defense. Permissions on it
  buy you nothing.
- <b>an unsigned `app.so.rt`</b> — plain text anyone can edit; it only prevents a *crash*, so a
  doctored manifest at worst lets an incompatible module *try* to load and fail. Sign it
  (`app.so.rt.sig`) and set a runtime trust if you need it to be trustworthy.

In one line: <b>protect the secret key on the signing side, and the trust file plus the
`cheatah` binary on the running side.</b> Those are the trust anchors; everything else is
either unforgeable or not a security boundary, so its permissions don't change the outcome.

### What this does and does NOT guarantee

- **Guarantees:** given an *untampered runtime* and a *trusted public key*, a module
  that is not signed by a trusted signer is refused. Tier 1 additionally catches
  non-malicious corruption with no key at all.
- **Does NOT guarantee:** it is not a defense against an attacker who can rewrite the
  `cheatah` runtime binary itself, or your trust file — the runtime is the trust anchor.
  Keep the secret key offline; anyone who has it can sign as you.

### Performance — what each tier costs

Verification is **paid once, when the module loads** — *before* `dlopen`, never during the
program's execution. So it's a fixed startup cost, not a per-call tax; for anything that
runs longer than a handful of milliseconds it disappears into the noise.

Measured with [`tests/benchmarks/integrity_bench.cpp`](https://github.com/BrofessorDoucette/cheatah/blob/main/tests/benchmarks/integrity_bench.cpp)
(Google Benchmark, calling `verify_module` directly — no process spawn), on `x86_64`; your
hardware will differ. Each row is the **total** load-time check, not just the increment:

| Tier enabled | 64 KiB module | 1 MiB module | what it adds |
|--------------|--------------:|-------------:|--------------|
| **Off** (no sidecars) | ~1.6 µs | ~1.6 µs | nothing — the module isn't even read |
| **+ checksum** (`.sha512`) | ~0.16 ms | ~2.8 ms | one **SHA-512** pass over the bytes (~400 MiB/s, scales with size) |
| **+ signature** (`.sig`, strict) | ~2.4 ms | ~7.4 ms | an **Ed25519 verify** (~2 ms, ~fixed) + the signature's own SHA-512 pass |
| **+ runtime manifest** (`.rt` + `.rt.sig`) | ~4.4 ms | ~9.2 ms | a **second Ed25519 verify** over the small manifest (~2 ms) |

Two things drive the numbers: the **SHA-512** pass scales with module size (cheatah's
from-scratch SHA-512 runs ~400 MiB/s — there's no SHA-512 CPU instruction on x86 the way
there is for SHA-256), and each **Ed25519 verification** is a roughly **fixed ~2 ms**.
cheatah's Ed25519 is implemented from scratch for **auditability and zero dependencies**,
not for raw throughput — an optimized library would verify in tens of microseconds, so if
you sign **many** modules in one short-lived process this is the cost to weigh. The runtime
header (`runtime/integrity.hpp`) documents the per-call `@complexity` and `@alloc` too.

The default stays **zero-overhead**: with no sidecars and no strict flag, the runtime never
reads the module to hash it (~1.6 µs is just the path canonicalization it already did).

## What you still have to worry about (for now)

cheatah does not yet protect you from these — they are your responsibility under the
single-trust model:

- **Running untrusted code.** There is **no sandbox**. A `.purr` program can do
  anything your user account can — treat a `.purr`/`.so` like a shell script you're
  about to `bash`: only run what you trust.
- <b>The `cpp { … }` escape hatch.</b> It runs **arbitrary C++** with no safety net —
  memory safety inside the block is entirely yours. (Slated to be **refused** in the
  future sandboxed mode, which does not exist yet.)
- <b>Host effects: `os.system`, filesystem, environment.</b> Python-parity conveniences
  with **full host access** today. Validate any data that flows into a path, a command,
  or a file you open — cheatah won't second-guess it for you.
- **Your own input validation and secrets.** Bounds checks stop memory corruption,
  not logic bugs. Untrusted *input* to a trusted program is still your domain:
  validate ranges, sanitize before shelling out, and keep credentials out of source.
- <b>`https://` authenticates the server by default.</b> The from-scratch `tls` 1.3 client (and
  `requests`/`websocket` riding it) validates the server's X.509 certificate chain to a trusted
  CA, matches the requested hostname against the certificate's `subjectAltName`, and checks the
  validity dates — so it resists an active **man-in-the-middle**, not just a passive
  eavesdropper. For a pinned/controlled peer you can opt out per-connection (`insecure`) or
  trust a specific CA bundle (`ca_file`). Chain signatures are verified for RSA-PKCS1 (SHA-256/384),
  ECDSA (SHA-256/384, P-256 or P-384 issuer keys), and Ed25519; algorithms cheatah doesn't
  implement yet (SHA-512, rsassa-PSS chain signatures) **fail closed**. Certificate revocation (OCSP/CRL) is not yet checked. The network **parsers**
  are likewise hardened against hostile remote data: TLS records, WebSocket frames, HTTP
  responses, and JSON are bounds-checked and size-capped so a malicious peer cannot corrupt
  memory, crash the client, or exhaust its memory.
- <b>`tls.accept` serves CA-trusted HTTPS with no other software.</b> The from-scratch TLS 1.3
  **server** presents an Ed25519 or ECDSA P-256 leaf (P-256 is what public CAs issue), sends
  the full certificate chain from a `fullchain.pem`, signs CertificateVerify only with an
  algorithm the client's `signature_algorithms` offered (RFC 8446 §4.4.3), refuses a cert/key
  mismatch before touching the socket, and fails closed on anything it does not implement —
  validated in both directions against OpenSSL in the system tests.
- **The path-sniff checks have a small TOCTOU window.** The world-writable / magic checks
  in *Validated module loading* stat the path and then `dlopen` it, with a tiny
  time-of-check/time-of-use gap. (The integrity verification above closes this gap: it
  hashes and loads the *same* open file descriptor.)
- **No capability gating / process isolation / MCP boundary yet.** Trust tiers,
  link-time capability gating, an OS-level sandbox (seccomp, Landlock, namespaces,
  rlimits), and a safe MCP server for LLM-driven cheatah are **design intent, not
  implemented**. Until they land, *don't* run LLM-generated or author-untrusted programs.

## Reporting

Please report suspected vulnerabilities **privately to the maintainer** rather than
opening a public issue.

---

The short version: the **compiler keeps generated code memory-safe and the toolchain
honest**, and the **QA gate keeps it that way**; **trust, host effects, and the escape
hatch are yours** until the sandboxed mode in
[`SECURITY.md`](https://github.com/BrofessorDoucette/cheatah/blob/main/SECURITY.md)
exists.
