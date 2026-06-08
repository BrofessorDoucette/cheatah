# Getting Started {#getting-started}

<div class="cheetah-slogan">🐱 <em>Write Python-shaped code; ship a native module.</em> 🐆</div>

cheatah is a **compiled, Python-shaped language**. You write a `.purr` file, compile
it with `purrc` into a native shared module (`.so`), and the `cheatah` runtime loads
and runs that module. This page walks the whole path: hello-world, then exactly what
happens when you press *compile* — and what "statically linked" and "dynamically
loaded" mean here.

## Hello, cheatah

```
import io

io.print("meow")
```

Save it as `hello.purr`, then:

```
purrc hello.purr -o hello.so     # compile to a native module
cheatah hello.so                 # the runtime loads & runs it
```

```
meow
```

`purrc` is the compiler; `cheatah` is the runtime host that loads a compiled module.
(Build both from source with the `release` preset: `cmake --preset release &&
cmake --build --preset release`.)

## What `purrc` does: `.purr` → C++ → `.so`

`purrc` is a transpiler in front of your system C++ compiler. It lexes and parses the
`.purr`, generates modern C++, and invokes the C++ backend **directly** (`fork` +
`execvp`, never a shell — no command injection) to produce the module:

```
   hello.purr
       │
       │  lex → parse → AST
       ▼
   ┌─────────────────────┐
   │  purrc  (transpiler) │   generates modern C++ (one .cpp)
   └─────────────────────┘
       │
       │  emits  hello.so.gen.cpp   (kept next to the output, so you can read it)
       ▼
   ┌─────────────────────────────────────────────────────────────┐
   │  C++ backend:  c++ -std=c++20 -O3 -march=native -fPIC -shared │
   │                +  libcheatah_<module>.a   (imported modules)  │
   └─────────────────────────────────────────────────────────────┘
       │
       ▼
   hello.so      ── a native shared object exporting:  extern "C" void purr_main()
```

Two things worth noticing:

- **It compiles at `-O3 -march=native`.** The module is real optimized native code
  for *your* CPU — that's the whole performance bargain (see @ref performance).
- **It links only what you `import`.** Each imported stdlib module (`io`, `ndarray`,
  `linalg`, …) contributes one static archive; a program that imports nothing pulls
  in nothing.

## Static *and* dynamic: the two kinds of linking

cheatah's module model uses **both** linking styles, each where it pays off:

```
                   ┌───────────────────────── hello.so ─────────────────────────┐
   STATIC LINK     │   your compiled code   +   libcheatah_io.a                  │
   (at compile)    │                            libcheatah_ndarray.a   (etc.)    │
                   │   the stdlib you imported is baked IN — the module is        │
                   │   self-contained, no separate cheatah libraries at runtime   │
                   └─────────────────────────────────────────────────────────────┘
                                        ▲
                                        │  dlopen("hello.so") + dlsym("purr_main")
                                        │
   DYNAMIC LOAD     ┌──────────────────────────────────────────────────────────┐
   (at run time)    │   cheatah  (the runtime host)                             │
                    │   validates the file, loads the module, calls purr_main() │
                    └──────────────────────────────────────────────────────────┘
```

- **Statically linked stdlib (compile time).** The standard-library modules you
  imported are compiled into the `.so` as static archives (`libcheatah_*.a`). The
  module is self-contained: there is no separate "cheatah runtime library" to ship or
  find on the load path.
- **Dynamically loaded module (run time).** The `.so` is **not** a standalone
  executable — it exports `extern "C" void purr_main()`. The `cheatah` host
  `dlopen`s the module, resolves `purr_main`, and calls it. Before loading, the
  runtime **validates** the file (canonical path, regular file, refuses
  world-writable, checks the ELF magic — see @ref security).

That `dlopen` step *is* the dynamic-loading mechanism interpreted languages use for
plugins and hot-reload — except the code being loaded is compiled native, so it runs
at full speed. (@ref performance covers how that gives you interpreter-style
dynamism without an interpreter.)

## Where to go next

- @ref porting — bring an existing Python script over with light edits.
- @ref performance — why the compile-time cost buys run-time speed, and how the
  `@perf` numbers are measured.
- @ref security — what the language protects you from, and what you still own today.
- Browse the **Modules** in the sidebar (`io`, `ndarray`, `linalg`, `string`, …) for
  the full standard-library reference.
