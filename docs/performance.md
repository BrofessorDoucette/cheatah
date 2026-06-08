# Performance {#performance}

<div class="cheetah-slogan">🐱 <em>Programs so fast they purrrrrrrrrrrrr like a kitten.</em> 🐆</div>

cheatah's whole reason to exist is **Python-shaped code at hand-written-C++ speed**.
That goal is a design constraint we apply *a priori* — before writing a feature — not
an afterthought we profile our way toward later. This page is the standing record of
how we keep cheatah fast, and the one deliberate price we pay for it: **slower
compilation**.

## The core bargain: compile-time cost for run-time speed

A `.purr` program is transpiled to modern C++ and built at **`-O3 -march=native`**,
then run on the headless host. We lean hard on **templates and C++20 concepts** for
zero-cost abstraction — generic code that monomorphizes at the call site into exactly
the machine code you'd write by hand, with no virtual dispatch, no boxing, no runtime
type tags.

Templates are not free: they make the C++ compiler do more work, so **cheatah
programs compile more slowly than an equivalent dynamically-typed language would
"load."** We accept that trade knowingly. Compilation happens once; the compiled `.so`
then runs in hot loops, inner products, and tight string-building paths millions of
times. Paying the cost once, at build time, to delete it forever at run time is the
right side of that trade for the scientific-computing, ML, and systems workloads
cheatah targets. (It is also why every emitted template is **constrained by a
concept** — see @ref constrain-all-templates below — so the extra compiler work buys
*comprehensible errors*, not just speed.)

## String building: no accidental O(n²), no surplus temporaries

This is the concern that prompted this page, and it is a real trap in naive
transpilers. Consider a typical builder — assembling an HTTP response header:

```python
fn response(status, ctype, body) {
    let nl = chr(13) + chr(10)
    let head = "HTTP/1.1 " + status + nl
    head = head + "Content-Type: " + ctype + nl
    head = head + "Content-Length: " + io.str(len(body)) + nl
    head = head + "Connection: close" + nl + nl
    return head + body
}
```

A literal translation of `head = head + "…" + ctype + nl` would **copy the entire
growing `head`** on every line. Over a header that's `n` bytes when done, that is
`O(n²)` total copying plus a fresh full-length temporary per statement — exactly the
death-by-`std::string`-temporaries you'd worry about.

cheatah does **not** emit that. Two things protect you:

1. **Self-append rewrite.** The codegen recognizes the pattern `x = x + e1 + e2 + …`
   (a plain variable at the head of a `+` chain that the appended operands don't
   re-read) and lowers it to **in-place appends**:

   ```cpp
   head += std::string("Content-Type: ");
   head += ctype;
   head += nl;
   ```

   The `head` buffer grows amortized in place; nothing copies the bytes already
   written. `O(n²)` becomes `O(n)`, and the per-line full-length temporary is gone.
   (The same rewrite turns numeric accumulators like `total = total + i` into
   `total += i`.)

2. **rvalue `operator+` chaining.** For a *fresh* left-to-right chain such as
   `let head = "HTTP/1.1 " + status + nl`, C++'s rvalue overloads of `operator+`
   reuse the leftmost temporary's buffer and append into it, so a chain of `k`
   concatenations is one growing buffer — not `k` independent allocations.

Net: the builder above performs work proportional to the **output length**, with the
allocations a careful C++ programmer would write by hand.

## Numeric speed: zero-cost generics + SIMD

- **`ndarray` is generic over its element type** (`basic_ndarray<T>`, constrained to a
  `Numeric` concept), monomorphized per type — an `int` array and a `double` array are
  each as tight as a hand-rolled `std::vector<T>` loop, with no shared dynamic base.
- **Element-wise kernels vectorize declaratively** via `std::transform(std::execution::unseq, …)`
  and `std::reduce(std::execution::unseq, …)` — we write our *intent to vectorize*
  in the source, and the compiler emits SIMD for whatever the target supports.
- **linalg kernels auto-vectorize** at `-O3 -march=native` (contiguous, unit-stride
  loops; no hand-written intrinsics). See @ref simd.hpp for the full SIMD model,
  including exactly what happens on a build with **no SIMD** (answer: identical
  results, just scalar/slower — SIMD is never a correctness dependency).

## Why constrained templates, given the compile-time cost {#constrain-all-templates}

Every generic surface in cheatah — `io.print`'s `Printable`, `ndarray`'s `Numeric`,
`math`'s `Ordered`, the baseline `Value` on every emitted function parameter — is
**concept-constrained**. This is a deliberate doubling-down on the compile-time
investment: we are already asking the compiler to instantiate templates, so we make
that work also yield *early, named* diagnostics ("`Point` does not satisfy
`Printable`") instead of pages of instantiation backtrace. Fast code **and** legible
errors, both bought with the same compile-time spend.

## The standing rule

When we add a feature, we ask up front: *does this allocate or copy more than the
hand-written C++ would?* If yes, we fix it in the codegen or the library before it
ships — as with the self-append rewrite — rather than leaving it for a user to
discover with a profiler. Performance is a feature, designed in, not bolted on.
