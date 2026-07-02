# Optimizations {#optimizations}

cheatah is a transpiler: every `.purr` becomes ordinary C++ that your system compiler
then optimizes like any other C++. But `purrc` does not lean on the backend for
everything — it rewrites your program into a *shape* the backend can make fast, and it
resolves at **compile time** anything it can prove is constant. The result is that
idiomatic, readable cheatah lowers to the same code you would have hand-written in C++,
without you thinking about it.

This page lists what `purrc` does for you. None of it changes observable behaviour
(side effects, ordering, and results are preserved); it only removes cost.

## Dead-variable elimination {#dead-variables}

A `let` whose variable is never read is **not emitted** — the binding disappears entirely.
Crucially this is *not* a blunt deletion: if the initializer has a side effect (a call), the
**side effect is kept** and only the unused variable is dropped, so semantics never change.

```purr
let unused = expensive()   # -> `expensive();`  (call kept, variable removed)
let n = 5                  # -> removed entirely (pure, never read)
```

Opt out with `purrc --no-remove-variables` (keeps every `let` verbatim — useful when
stepping through generated C++ in a debugger).

## In-place string and accumulator building — no accidental O(n²) {#in-place-append}

The classic interpreted-language trap is `s = s + a + b + …` in a loop: each `+` copies the
whole accumulator, turning an O(n) build into O(n²). `purrc` recognizes a **self-append** —
an assignment whose right-hand side is a `+`-chain beginning with the target itself — and
flattens it into in-place appends:

```purr
s = s + part_a + part_b    # -> s += part_a; s += part_b;   (no full-length temporaries)
```

So the build is O(n) with no surplus allocations, the same as if you had written the
in-place appends by hand. This fires for strings and for arithmetic accumulators alike.

## Everything passes by reference — no hidden copies {#pass-by-reference}

Every function parameter lowers to a **forwarding reference** (`auto&&`), so an argument
binds *without copying*: strings, structs, lists, dicts, and ndarrays are never duplicated
at a call boundary. A temporary still binds and lives for the duration of the call, so you
never reason about lifetimes. In-place mutation through a parameter is visible to the caller
— exactly Python's object semantics, but with zero copy cost. ndarray parameters in
concrete (typed) functions bind by mutable reference specifically so element updates reach
the caller in place.

## Compile-time evaluation {#compile-time}

Anything `purrc` can prove constant is resolved while compiling, not while running:

- **`if constexpr (…)`** and a `match` over a constant subject lower to compile-time
  branch selection — the dead arm is discarded, never compiled into the binary.
- **`constexpr let` / `constexpr fn`** make a binding or a function a compile-time constant;
  an `if`/`match` over such a value then auto-lowers to its `if constexpr` form with no
  keyword needed.
- A **`match` over an integer subject** lowers to a real C++ `switch` (a jump table) rather
  than an `if`/`else-if` ladder; string/float/runtime subjects keep the equality chain,
  which is the only form the C++ compiler accepts for them.

See [Compile-time `if` and `match`](#compile-time) above and the language guide for the
surface syntax.

## A shape the backend can fully optimize {#backend-friendly}

- **Internal linkage.** Program functions are emitted `static`, so the C++ optimizer is
  free to inline and specialize them with no exported-symbol interposition barrier.
- **Constrained templates, monomorphized.** Untyped parameters become C++20 abbreviated
  function templates constrained by a concept, so each call site compiles to a concrete,
  fully-typed, individually-optimized instantiation — generic source, specialized code.
- **String views, not copies, across module calls.** Where a stdlib entry point accepts a
  string view, `purrc` threads the argument through directly with no intermediate
  `std::string` materialized.

## What this means for you

You write the clear version. `purrc` removes the dead bindings, kills the quadratic string
builds, elides the copies, folds the constants, and hands the backend code it can inline.
The readable program and the fast program are the same program — which is the whole point.
