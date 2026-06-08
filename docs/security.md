# Security {#security}

<div class="cheetah-slogan">🐱 <em>Memory-safe by construction; honest about the rest.</em> 🐆</div>

This page is the **user's-eye view** of cheatah security: what the language and
toolchain protect you from automatically, and what you are still on the hook for
**today**. For the full threat model, the standing security review, and the
sandbox/MCP roadmap, see
[`SECURITY.md`](https://github.com/BrofessorDoucette/cheatah/blob/main/SECURITY.md).

## The one assumption to internalize

cheatah is **single-trust today**, exactly like CPython or a C++ compiler:

> The `.purr` source and the compiled `.so` you run are **fully trusted**. The
> person who writes, compiles, and runs the program is the person whose machine it
> runs on.

cheatah is **pre-alpha**. There is **no sandbox yet** — so the headline rule is:
**do not run `.purr` you did not write or audit.** Everything below is framed by
that assumption.

## Protections built into the language

These you get for free — the compiler and runtime enforce them so you don't have to:

- **Memory-safe generated code by construction.** Codegen emits value types, STL
  containers, and smart pointers — **no raw `new`/`delete`, no manual pointer
  arithmetic**. Use-after-free and double-free are off the table for the code the
  compiler writes. (The one exception is the `cpp { … }` escape hatch — see below.)
- **Bounds- and overflow-checked standard library.** Array shape math is
  overflow-checked (a `size_t` overflow throws instead of under-allocating), negative
  dimensions/indices are rejected, and element access is bounds-checked — malformed
  inputs **raise an error, they don't corrupt memory**.
- **No command injection in the toolchain.** `purrc` invokes the C++ backend with
  `fork` + `execvp` — **never a shell** — so a crafted path can never be reinterpreted
  as shell syntax.
- **Validated module loading.** Before `dlopen`-ing a compiled module the runtime
  canonicalizes the path, requires a **regular file**, **refuses world-writable**
  modules, and checks the **ELF magic** — so a stray or obviously-tampered `.so`
  is rejected rather than blindly loaded.
- **Every template is concept-constrained.** Misuse surfaces as an **early, named
  compile error** ("`Point` does not satisfy `Printable`"), not undefined behavior or
  a pages-long instantiation backtrace.
- **A QA gate that runs on every push** (a local pre-push hook). The suite runs under
  **AddressSanitizer + UndefinedBehaviorSanitizer** *and* **Valgrind**, with **100%
  line and function coverage of the standard library** enforced — memory errors, UB,
  and leaks fail the build before they ship. (Caveat: sanitizers only cover *exercised*
  paths; the historical `ndarray` overflow bugs were caught by manual review, then
  fixed and regression-tested.)

## What you still have to worry about (for now)

cheatah does not yet protect you from these — they are your responsibility under the
single-trust model:

- **Running untrusted code.** There is **no sandbox**. A `.purr` program can do
  anything your user account can. Treat a `.purr`/`.so` like a shell script you're
  about to `bash`: only run what you trust.
- **The `cpp { … }` escape hatch.** It runs **arbitrary C++** with no safety net —
  memory safety inside the block is entirely yours. (It is slated to be **refused**
  in the future sandboxed mode, but that mode does not exist yet.)
- **Host effects: `os.system`, filesystem, environment.** These are Python-parity
  conveniences with **full host access** today. Validate any data that flows into a
  path, a command, or a file you open — cheatah won't second-guess it for you.
- **Your own input validation and secrets.** Bounds checks stop memory corruption,
  not logic bugs. Untrusted *input* to a trusted program is still your domain:
  validate ranges, sanitize before shelling out, and keep credentials out of source.
- **A known TOCTOU window on module load.** Between the runtime's file checks and the
  `dlopen` there is a small time-of-check/time-of-use gap. It is **acceptable under
  full trust** but is not a defense against an attacker who can swap files underneath
  you.
- **No capability gating / process isolation / MCP boundary yet.** Trust tiers,
  link-time capability gating, an OS-level sandbox (seccomp, Landlock, namespaces,
  rlimits), and a safe MCP server for LLM-driven cheatah are **design intent, not
  implemented**. Until they land, *don't* run LLM-generated or otherwise
  author-untrusted programs.

## Reporting

Please report suspected vulnerabilities **privately to the maintainer** rather than
opening a public issue.

---

The short version: the **compiler keeps the code it generates memory-safe and the
toolchain honest**, and the **QA gate keeps it that way**; **trust, host effects, and
the escape hatch are yours** until the sandboxed mode in
[`SECURITY.md`](https://github.com/BrofessorDoucette/cheatah/blob/main/SECURITY.md)
exists.
