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

- **`os.system`, raw filesystem access, process/env access** — Python-parity
  conveniences for your own scripts.
- **The `cpp { … }` escape hatch** — runs arbitrary C++. Memory safety is the
  author's responsibility inside it (see [README](README.md#the-language)).
- **No sandbox** — a cheatah program can do anything its user can.

What we still hold ourselves to *even under full trust* — because bugs are bugs:

- **Generated code is memory-safe** by construction (value types and STL containers;
  no raw `new`/`delete`, no manual pointer arithmetic) — except inside `cpp { … }`.
- **No command injection in the toolchain.** `purrc` invokes the C++ backend with
  `fork` + `execvp` (no shell), so paths can never be reinterpreted as shell syntax.
- **The runtime validates a module before `dlopen`** (`runtime/main.cpp`):
  canonicalizes the path, requires a regular file, refuses world-writable modules,
  and checks the ELF magic.
- **Optional integrity verification before load** (`runtime/integrity.cpp`): a
  SHA-256 checksum (corruption) and an Ed25519 signature (tampering), plus a
  build-runtime compatibility check — opt-in, fail-closed, and fd-bound to the bytes
  it verified.
- **Standard-library bounds/overflow checks** so malformed inputs raise instead of
  corrupting memory (see the review below).
- **The QA gate runs the whole suite under ASan + UBSan *and* Valgrind**
  (`security/run-valgrind.sh`), with 100% stdlib line+function coverage — memory
  errors, UB, and leaks fail the build.

## Standing security review

| Area | Finding | Status |
|------|---------|--------|
| `ndarray` shape math | `product()` could overflow `size_t` → under-allocation → OOB writes | **Fixed** — overflow-checked, throws |
| `ndarray` dims/indices | negative `long long` → huge `size_t`; no index bounds check (OOB read) | **Fixed** — rejects negatives, bounds-checks `at()` |
| `purrc` backend invocation | shell metacharacter / command injection | **Safe** — `fork`+`execvp`, no shell |
| runtime module load | loading a wrong/tampered `.so` | **Mitigated** — canonical path + regular-file + world-writable + ELF-magic checks; plus **optional** SHA-256 + Ed25519 verification before load (`CHEATAH_VERIFY=strict`). The path-sniff checks have a small TOCTOU window; the integrity verification does **not** — it hashes and loads the same open fd (`/proc/self/fd`). |
| module authenticity | running a substituted/untrusted-signer module | **Mitigated (opt-in)** — strict mode requires a valid Ed25519 signature from a pinned trusted key, fail-closed and non-downgradable. Not a defense if the attacker can rewrite the runtime or the trust file. |
| `cpp { … }` escape hatch | arbitrary C++ / no memory safety | **By design** under full trust; **disabled** in the sandboxed mode proposed below |
| `os.system` / raw fs / env | arbitrary host effects | **By design** under full trust; **capability-gated** in sandboxed mode |

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
  `ndarray`, `linalg`, pure `datetime`, seeded `random`, `hashlib`, and a
  **capability-mediated** `io`/`fs`).
- Dangerous capabilities (`proc:spawn`/`os.system`, `net:*`, unrestricted `fs:*`,
  `env:*`) are **not linked at all** unless the host grants them — so they're absent
  from the binary, not merely unused.
- Granted capabilities are **scoped** (e.g. `fs:read=/srv/data`, no write, no exec).

**3. The `cpp { … }` escape hatch is refused in sandboxed mode.**
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
- **`rlimit`s** (CPU, address space, file size, no core dumps) and a wall-clock
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
