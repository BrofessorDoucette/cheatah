# Why cheatah? {#why}

<div class="cheetah-slogan">🐱 <em>Not another language for the sake of it.</em> 🐆</div>

We already know the world has too many programming languages. The last thing the
world needs is *another* language to learn, package, and argue about on the internet.

So cheatah is not built on a whim. It comes from watching where the popular tools
quietly let teams down once a project outgrows a weekend — and has to ship, run fast,
and keep running.

## The trouble with the comfortable default

Python is a joy to start with. It is also, by design, **dynamically typed and
interpreted**, and those two choices have a long tail of cost:

- **Bugs hide until run time.** With no compiler checking types ahead of time, a typo,
  a wrong shape, or a `None` where you expected a value surfaces as a crash in
  production. Tracking those down eats the time the language was supposed to save you.
- **Interpreted means inherently slower.** Without a compiler turning your program into
  optimized native code, every line pays an interpreter tax.
- **Interpreted is also inherently *less* secure.** A program assembled and typed at run
  time gives an attacker more surface and more ways for malformed input to become
  malformed behavior, and far less that can be proven *before* the code runs.
- **The dependency story does not age well.** Much of the ecosystem is poorly
  maintained, and projects pull in long chains of it. Software that has to last needs
  dependencies that are reliable, auditable, and few.

None of this is a knock on Python for what it is good at. It is an honest accounting of
what happens when you ask it to carry software that has to be **long-lived.**

## A different world than these languages were designed for

Most of today's mainstream languages were designed for a world that no longer exists.
We now live in a world where **AI-amplified threats** are becoming dramatically more
dangerous. Unskilled adversaries (script kiddies) have access to knowledge and tooling they
simply did not have before: an exploit or a piece of malware that once took a skilled adversary weeks to
build can now be **replicated in a few days** with the help of a model.

Two things follow from that, and they shape everything about cheatah:

- **Readability matters more than ever.** As programmers move away from typing every
  line by hand and lean on AI to generate code, the bottleneck shifts to *verifying that
  the generated code is correct*. Code you cannot read quickly, you cannot trust quickly.
  cheatah is deliberately Python-shaped and transpiles to **clean, inspectable C++** so a
  human (or another model) can audit what actually runs.
- **Memory and type safety matter more than ever.** The same AI wave has amplified
  cybersecurity attacks, so the classes of bug that turn into vulnerabilities — memory
  corruption, type confusion — are exactly the ones you can no longer afford to ship.

## What cheatah does about it

cheatah is a Python-like language that is **compiled, statically typed, and
memory-safe**, built for the world we are actually in:

- **Statically checked by default.** Every type is checked at compile time. Dynamic
  typing is **not implemented today**, and if it is ever added it will be strictly
  **opt-in** — the safe, checkable path is the default, not an afterthought.
- **Memory safety without a garbage collector.** Because programs are restricted to
  cheatah, the compiler emits value types, STL containers, and smart pointers — no raw
  `new`/`delete`, no manual pointer arithmetic — so whole classes of memory bugs are off
  the table, and with no GC, none of its pauses.
- **Native speed.** A real compiler turns `.purr` into optimized native code, so you get
  performance in the neighborhood of hand-written C++ — see the
  [Performance](performance.html) guide.
- **Tamper-evident binaries, optionally.** cheatah ships **just enough lightweight,
  modern cryptography** — a SHA-256 checksum, Ed25519 code signatures, and a signed
  build-runtime manifest, all implemented from scratch in the standard library with **no
  external crypto dependency** — to verify a compiled module against **corruption,
  injection, and an incompatible C runtime** before it loads. Off by default; turn it on
  when you need to *know* the binary you're running is the one you built. See
  [Security](security.html).

## Transpiled, transparent, and staying that way

cheatah **is — and will remain — a transpiled language.** `purrc` lowers your `.purr`
to readable C++ and hands it to the **system C++ compiler**. That is a deliberate design
choice with real, lasting benefits:

- **You inherit the platform toolchain for free.** Because the final step is your own
  system compiler, updates to the **C/C++ runtime and standard library** — new
  optimizations, security hardening, CPU targets, platform fixes — are picked up
  automatically. cheatah doesn't have to chase the platform; it rides on it.
- **Everything is transparent — no new mental model required.** The generated C++ is
  there to read. Editor extensions can surface, at a **systems level**, exactly what a
  line of `.purr` becomes: which calls, which types, which allocations. It's
  **transparency without having to learn something new** — the thing underneath is just
  C++.
- **You can trace it to the hardware.** A systems programmer or a real-time engineer
  should be able to follow what a cheatah script actually does on the machine — the
  memory it touches, the work it issues — **as if they had written it in C or C++**,
  because in the end, that's what runs. No opaque bytecode, no hidden interpreter loop
  between you and the silicon.

This is why cheatah can be both approachable *and* trustworthy: the friendly,
Python-shaped surface never costs you visibility into what the machine is doing.

That is the whole pitch: a language you can **read**, a compiler you can **trust**, and
binaries you can **verify** — built deliberately, for software meant to last.

---

Ready to write some `.purr`? Head to the **[Getting started](getting-started.html)**
guide. Want the safety details first? Read **[Security](security.html)**.

<div class="cheetah-slogan">🐆 Built for speed. Guarded for safety. 🐱</div>
