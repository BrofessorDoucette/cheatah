# Optimizations {#optimizations}

cheatah is a transpiler: every `.purr` becomes ordinary C++ that your system compiler
then optimizes like any other C++. But `purrc` does not lean on the backend for
everything — it rewrites your program into a *shape* the backend can make fast, and it
resolves at **compile time** anything it can prove is constant. The result is that
idiomatic, readable cheatah lowers to the same code you would have hand-written in C++,
without you thinking about it.

Every "generated C++" block below is <b>real `purrc` output</b> — you can reproduce it by
running `purrc your.purr -o your.so` and reading the `your.so.gen.cpp` it writes next to
the module. None of these transforms change observable behaviour (side effects, ordering,
and results are preserved); they only remove cost.

## Dead-variable elimination {#dead-variables}

A `let` whose variable is never read is **not emitted** — the binding disappears entirely.
Crucially this is *not* a blunt deletion: if the initializer has a side effect (a call), the
**side effect is kept** and only the unused variable is dropped, so semantics never change.

```purr
fn describe(x) {
    let logged = io.str(x)   # a call — its effect is kept
    let scratch = 42          # pure, never read — removed
    return x
}
```

SIDEBYSIDE: purrc --no-remove-variables | purrc (default)

```cpp
static auto describe(builtins::Value auto&& x) {
    auto logged = io::str(x);
    auto scratch = 42LL;
    return x;
}
```

```cpp
static auto describe(builtins::Value auto&& x) {
    io::str(x);
    return x;
}
```

The `io::str(x)` **call survives** (it could have effects); only the unread `scratch` and
the now-nameless `logged` binding vanish. Opt out with `purrc --no-remove-variables` (the
left column) when stepping through generated C++ in a debugger.

## In-place accumulation — no accidental O(n²) {#in-place-append}

The classic interpreted-language trap is `s = s + a + b + …` in a loop: a naive translation
copies the **whole growing accumulator** on every `+`, turning an O(n) build into O(n²)
plus a fresh full-length temporary per statement. `purrc` recognizes a **self-append** — an
assignment whose right-hand side is a `+`-chain that begins with the target itself — and
lowers it to in-place appends:

```purr
fn build(parts) {
    let s = ""
    for p in parts {
        s = s + p + "\n"     # self-append: `s` leads the + chain
    }
    return s
}
```

SIDEBYSIDE: a naive transpiler (copy per +) | purrc (in-place)

```cpp
// what "s = s + p + \n" would cost naively:
s = ((s + p) + "\n");   // copies all of s each iteration -> O(n²)
```

```cpp
static auto build(builtins::Value auto&& parts) {
    auto s = std::string("");
    for (auto& p : parts) {
        (s += p) += "\n";   // amortized in-place -> O(n)
    }
    return s;
}
```

The `s` buffer grows amortized in place; nothing copies the bytes already written. The same
rewrite turns numeric accumulators like `total = total + i` into `total += i`. And for a
*fresh* left-to-right chain (`let head = a + b + c`), C++'s rvalue `operator+` overloads
reuse the leftmost temporary's buffer — `purrc` emits `auto head = ((a + b) + c);`, one
growing buffer, not `k` independent allocations. Net: work proportional to the **output
length**, the allocations a careful C++ programmer would write by hand.

## Everything passes by reference — no hidden copies {#pass-by-reference}

Every function parameter lowers to a **forwarding reference** (`auto&&`), so an argument
binds *without copying*: strings, structs, lists, dicts, and ndarrays are never duplicated
at a call boundary. In the examples above, every signature is
`f(builtins::Value auto&& …)` — that `auto&&` is the no-copy binding. A temporary still
binds and lives for the duration of the call, so you never reason about lifetimes; in-place
mutation through a parameter is visible to the caller — exactly Python's object semantics,
but with zero copy cost. (A naive lowering to by-value `std::string`/`std::vector`
parameters would deep-copy every argument at every call; cheatah never does.) ndarray
parameters in concrete (typed) functions bind by mutable reference specifically so element
updates reach the caller in place.

## Branch selection: the fastest legal lowering {#compile-time}

`purrc` inspects the subject of every `if` and `match` and emits the cheapest form the C++
backend will accept, trying three lowerings **in order**:

<b>1 — Provably constant? → `if constexpr` (the branch is chosen at compile time).</b> When the
condition is built from `constexpr` values (`constexpr let`, `constexpr fn`), purrc detects it
and lowers the whole `if`/`elif`/`else` to <b>`if constexpr`</b> — no keyword needed at the call
site. The winning arm is selected *while compiling*, and **the dead arms are never compiled
into the binary at all**:

```purr
fn f() {
    constexpr let MODE = 2
    if MODE == 1 { io.print("one") }
    elif MODE == 2 { io.print("two") }
    else { io.print("other") }
}
```

```cpp
static auto f() {
    constexpr auto MODE = 2LL;
    if constexpr (MODE == 1LL) {
        io::print("one");
    } else if constexpr (MODE == 2LL) {
        io::print("two");
    } else {
        io::print("other");
    }
}
```

Only the `"two"` arm survives into the object code; the `"one"` and `"other"` arms are
discarded before codegen — zero runtime cost and zero binary weight.

<b>2 — Not constant, but an integer `match`? → `switch` (a jump table).</b> An integer `match`
becomes a real C++ `switch` — O(1) dispatch no matter how many cases:

```cpp
static auto label(builtins::Value auto&& n) {
    auto _purr_match_0 = n;
    switch (_purr_match_0) {
        case 1LL: { return std::string("one"); }
        case 2LL: { return std::string("two"); }
        default:  { return std::string("many"); }
    }
}
```

<b>3 — Otherwise → an `if` / `else-if` chain.</b> A `match` over a string/float/runtime subject
(or a plain runtime `if`) keeps the sequential equality-test ladder — the only form C++ accepts
for a non-integer subject:

```cpp
static auto kind(builtins::Value auto&& s) {
    auto _purr_match_0 = s;
    if (_purr_match_0 == std::string("a")) {
        return 1LL;
    } else {
        return 0LL;
    }
}
```

You write the same readable `if`/`match` every time; purrc silently walks (1) → (2) → (3), so
you get compile-time elimination when it is provable, a jump table when it is legal, and a
correct equality chain otherwise — never paying for a form more expensive than the subject
requires.

## Numeric speed: zero-cost generics + SIMD {#numeric-simd}

- <b>`ndarray` is generic over its element type</b> (`basic_ndarray<T>`, constrained to a
  `Field` concept — real or complex), monomorphized per type: an `int` array, a `double`
  array, and a `complex<double>` array are each as tight as a hand-rolled `std::vector<T>`
  loop, with no shared dynamic base.
- **Element-wise kernels vectorize declaratively** via `std::transform(std::execution::unseq, …)`
  and `std::reduce(std::execution::unseq, …)` — we write the *intent to vectorize* in the
  source and the compiler emits SIMD for whatever the target supports. It is feature-test
  guarded (`__cpp_lib_execution`), so a toolchain without `<execution>` (e.g. Apple libc++)
  falls back to the policy-less overloads — same results, and `-O3 -march=native` still
  auto-vectorizes; `unseq` adds no threads or TBB dependency.
- **linalg kernels auto-vectorize** at `-O3 -march=native` (contiguous, unit-stride loops;
  no hand-written intrinsics). See @ref simd.hpp for the full SIMD model, including exactly
  what happens on a build with **no SIMD** (answer: identical results, just scalar/slower —
  SIMD is never a correctness dependency).

## Smaller footprint: opt-in sized integers {#sized-integers}

A cheatah `int` is a 64-bit `long long` — the right default for a counter or an index, wrong
for a million-element column that only ever holds small values. So a **width is opt-in per
declaration**: annotate a type with an explicit-width integer and the *storage* shrinks with
no change to speed.

```
let ages: list<u8> = [31, 44, 27]     # 1 byte / element (std::uint8_t), not 8
let ids:  list<i32> = load_ids()      # 4 bytes / element
struct Cell { x: u8, y: u8 }          # sizeof(Cell) == 2, not 16
io.print(sizeof(i16), sizeof(Cell))   # -> 2 2   (proven in-language)
```

- **Widths.** `i8`/`i16`/`i32`/`i64` (signed) and `u8`/`u16`/`u32`/`u64` (unsigned), each also
  spellable in full (`int8`…`uint64`) or with the original C library name (`int8_t`…`uint64_t`)
  — all the **same** `<cstdint>` exact-width type by construction, so name it however you like.
  Usable anywhere a type appears: scalars, `list`/`dict`/`array` elements, `ndarray` elements,
  and struct fields.
- **Zero runtime cost.** A width lowers straight to `std::int32_t`/`std::uint8_t`/…: contiguous,
  trivially copyable, SIMD-friendly, no tag and no box. In arithmetic, narrow operands **promote
  to 64-bit for free** (ordinary C++ integer promotion) and only truncate back on store — so the
  compute is exactly as fast as `int`, and only the *stored* value is small.
- <b>`int` never changes.</b> It stays 64-bit, so standalone integers — loop counters, `++`/`--`,
  literals — keep their speed and range. Narrowing is opt-in and never happens implicitly.
- **Semantics, all at compile time.** Narrow storage wraps at its width and a wide result
  truncates on store (as in C / NumPy fixed-width types). A literal that does not fit its width
  is a **compile-time error** (`300` into an `i8` will not build) — the only bounds check, and it
  costs nothing at runtime. This is `pandas`-style downcasting decided by the *type* up front,
  not a runtime re-encoding: no bit-packing, no per-value metadata, nothing to decode on read.

## A shape the backend can fully optimize {#backend-friendly}

- **Internal linkage.** Program functions are emitted `static` (note every `static auto`
  above), so the C++ optimizer is free to inline and specialize them with no
  exported-symbol interposition barrier.
- **Constrained templates, monomorphized.** Untyped parameters become C++20 abbreviated
  function templates constrained by a concept (`builtins::Value auto&&`), so each call site
  compiles to a concrete, fully-typed, individually-optimized instantiation — generic
  source, specialized code.
- **String views, not copies, across module calls.** Where a stdlib entry point accepts a
  string view, `purrc` threads the argument through directly with no intermediate
  `std::string` materialized.

## Why constrained templates, given the compile-time cost {#constrain-all-templates}

Every generic surface in cheatah — `io.print`'s `Printable`, `ndarray`'s `Numeric`,
`math`'s `Ordered`, the baseline `Value` on every emitted parameter — is
**concept-constrained**. A deliberate doubling-down on the compile-time investment: we're
already instantiating templates, so we make that work also yield *early, named* diagnostics
("`Point` does not satisfy `Printable`") instead of pages of backtrace. Fast code **and**
legible errors, from the same compile-time spend.

## What this means for you

You write the clear version. `purrc` removes the dead bindings, kills the quadratic string
builds, elides the copies, folds the constants, and hands the backend code it can inline.
The readable program and the fast program are the same program — which is the whole point.
For how that translates into measured speed against Python, NumPy, and OpenSSL, see
[Performance](performance.html).
