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
  containers (`std::string`, `std::vector`, `std::unordered_map`, …) — **no raw
  `new`/`delete`, no manual pointer arithmetic** — so use-after-free and double-free are
  off the table for compiler-written code. (The one exception is the `cpp { … }` escape
  hatch — see below.)
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
  check a **SHA-256 checksum** (corruption), an **Ed25519 signature** (tampering), and a
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

### Tier 1 — checksum (accidental corruption)

A `<module>.sha256` sidecar (sha256sum-compatible) is **auto-verified when present**: if
the bytes don't match, the runtime refuses — catching disk corruption, a truncated
download, or a botched copy.

```sh
purrc app.purr -o app.so --checksum     # writes app.so.sha256
cheatah app.so                          # auto-checks the checksum, then runs
sha256sum -c app.so.sha256              # …and ordinary tools can check it too
```

### Tier 2 — Ed25519 signature (deliberate tampering)

A signature proves the module was produced by the holder of a **secret key** and has
not changed since. You sign with a private key kept **offline**; the runtime verifies
with the matching **public key**, which you pin in a trust file. An attacker who cannot
sign with a trusted key cannot get a tampered module accepted.

```sh
purrc --keygen release            # -> release.key (SECRET, mode 0600) + release.pub
purrc app.purr -o app.so --sign release.key   # writes app.so.sig (+ app.so.sha256)

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

### Tier 3 — build-runtime compatibility (a separate key)

A module dynamically links the host's C/C++ runtime (glibc, libstdc++); built against a
**newer** runtime than the host provides, `dlopen` fails cryptically or misbehaves. cheatah
records the build runtime — CPU arch, glibc version, libstdc++ ABI — in a small text sidecar
next to the module called the **runtime manifest**, named **`<module>.rt`** (`.rt` for
run**t**ime), and checks it against the **live host** before loading, refusing with a
readable reason.

```sh
purrc app.purr -o app.so --runtime    # writes app.so.rt  (the runtime manifest)
cheatah app.so                        # refuses e.g. "needs glibc >= 2.39, host has 2.31"
```

The manifest can be signed with a **key separate from the code-signing key** — so *what the
code is* and *what it was built against* are vouched for independently, and neither key can
stand in for the other:

```sh
purrc --keygen code-key            # the code-signing keypair
purrc --keygen runtime-key         # a DISTINCT runtime keypair
# --sign writes app.so.sig (code); --sign-runtime writes app.so.rt + app.so.rt.sig (runtime)
purrc app.purr -o app.so --sign code-key.key --sign-runtime runtime-key.key

export CHEATAH_VERIFY=strict
export CHEATAH_TRUST=code-key.pub             # the code-signing trust
export CHEATAH_RT_TRUST=runtime-key.pub       # the SEPARATE runtime trust
cheatah app.so
```

The compatibility check runs whenever a `.rt` is present (it prevents a crash, so it isn't
gated on strict mode); the *signature* is required only in strict mode when a runtime trust
(`CHEATAH_RT_TRUST` / `--trust-runtime`) is set.

### What this does and does NOT guarantee

- **Guarantees:** given an *untampered runtime* and a *trusted public key*, a module
  that is not signed by a trusted signer is refused. Tier 1 additionally catches
  non-malicious corruption with no key at all.
- **Does NOT guarantee:** it is not a defense against an attacker who can rewrite the
  `cheatah` runtime binary itself, or your trust file — the runtime is the trust anchor.
  Keep the secret key offline; anyone who has it can sign as you.

### Performance

Verification is **off by default and zero-overhead** when there are no sidecars: the
runtime doesn't even read the module to hash it. When enabled, the cost is one SHA-256
pass over the file (memory-bandwidth bound, ~GB/s) plus, for the signed tier, a single
Ed25519 verification (well under a millisecond) — paid once at load, never during
execution. The runtime header (`runtime/integrity.hpp`) documents the per-call
`@complexity` and `@alloc` so you can judge the cost before turning it on.

## What you still have to worry about (for now)

cheatah does not yet protect you from these — they are your responsibility under the
single-trust model:

- **Running untrusted code.** There is **no sandbox**. A `.purr` program can do
  anything your user account can — treat a `.purr`/`.so` like a shell script you're
  about to `bash`: only run what you trust.
- **The `cpp { … }` escape hatch.** It runs **arbitrary C++** with no safety net —
  memory safety inside the block is entirely yours. (Slated to be **refused** in the
  future sandboxed mode, which does not exist yet.)
- **Host effects: `os.system`, filesystem, environment.** Python-parity conveniences
  with **full host access** today. Validate any data that flows into a path, a command,
  or a file you open — cheatah won't second-guess it for you.
- **Your own input validation and secrets.** Bounds checks stop memory corruption,
  not logic bugs. Untrusted *input* to a trusted program is still your domain:
  validate ranges, sanitize before shelling out, and keep credentials out of source.
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
