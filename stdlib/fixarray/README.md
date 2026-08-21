# cheatah `fixarray`

Fixed-extent vectors and matrices — the same mathematics as an `ndarray::NDArray`, with the
shape moved into the type, for the small-and-hot regime (a renderer's transforms, a physics
solver's contact frames, a filter's small state). Header-only; `import fixarray` (it auto-links
`ndarray` for its element concepts). The heavy shape-generic numerics on the dynamic `NDArray`
(LU/QR/SVD/eigen) live in the sibling [`linalg`](../linalg/README.md) module.

## The type

An `NDArray` carries its shape at runtime and its elements on the heap. That is what
makes it general — and, for a 3-D direction or a 4×4 transform, it is the whole cost:
an allocation and an indirection to move sixteen floats.

`fixarray::Fixed<T, Dims...>` is the same idea with the shape in the type. The elements
live inline, so the value is trivially copyable and nothing allocates; the loops have
compile-time trip counts and auto-vectorize. **These are the types for high-performance
work** — transforms, contact frames, small filter state — where the same matrix is
built and consumed millions of times a second. Everything else matches `NDArray`: the
element types, the `(row, column)` index, numpy's vocabulary, and the answers.

```cpp
using namespace cheatah::fixarray;
vec3f up{0.0F, 1.0F, 0.0F};          // 12 bytes, no allocation
mat4f mvp = projection * view;       // 64 bytes — exactly a push constant
vec3f n = normalize(cross(up, dir));
```

Aliases: `vec2f`…`vec4f`, `vec2d`…`vec4d`, `mat2f`…`mat4f`, `mat2d`…`mat4d`;
`Vec<T,N>` and `Mat<T,R,C>` for anything else. Rank 1 (vector) and rank 2 (matrix).

### From cheatah

`import fixarray` and use it from a `.purr` program — construct with the call form and operate
with the free functions:

```
import io
import fixarray

fn length2(v: fixarray.Fixed<f32, 3>) -> f32 {   # module-qualified param/return
    return fixarray.dot(v, v)
}

fn main() {
    let a = fixarray.vec3f(1.0, 2.0, 3.0)        # construct (deduced type)
    let b: fixarray.vec3f = fixarray.vec3f(4.0, 5.0, 6.0)   # or annotate the type
    io.print(fixarray.dot(a, b))                 # 32
    let bytes: fixarray.Vec<u8, 3> = fixarray.Vec<u8, 3>(1, 2, 3)  # narrow element (1 byte each)
    io.print(length2(a))                         # 14
}
main()
```

Aliases (`vec3f`, `mat4f`, …) and the explicit `fixarray.Fixed<T, Dims…>` / `fixarray.Vec<T, N>`
spellings all work as `let`/field/parameter/return annotations. A parameter of a fixarray type
binds by reference, so pass a **named** vector (an lvalue) to your own functions. `import fixarray`
alone pulls in `ndarray` transitively.

**A matrix is stored column-major**, unlike `NDArray`. The indexing you write does not
change, but `data()` hands back columns. That is deliberate and measured: `m * v`
becomes a sum of scaled columns — contiguous and vertical — instead of four horizontal
dot products behind a shuffle network, and the buffer is already in the order GPU APIs
expect, so uploading a transform is a copy rather than a transpose.

### Speed

Benchmarked against [GLM](https://github.com/g-truc/glm) across the **complete overlap
of the two APIs** — 160 pairs: every vector and matrix operation (arithmetic, `dot`,
`cross`, `length`, `normalize`, `distance`, `reflect`, `min`/`max`/`clamp`, `mix`,
`step`/`smoothstep`, `matmul`, `transpose`, `determinant`, `inverse`, `matrixCompMult`,
`outerProduct`, `inverseTranspose`), at sizes 2/3/4, in both `float` and `double`
([`fixed_glm_bench.cpp`](../../tests/benchmarks/fixed_glm_bench.cpp)). Both sides compile in
the same translation unit with the same flags, so neither gets an instruction set the other
lacks — and the benchmark **verifies the two produce identical results before it times
either**, so a fast wrong answer can never masquerade as a win.

`Fixed` is **faster than or at parity with GLM on every operation, and slower on none** —
the exact tally is in the generated table below, because a count restated in prose is a count
that drifts the moment someone re-measures. A representative sample:

<!-- BENCH:fixarray-vs-glm-highlights begin -->
<!-- cheatah-bench-stamp v1
     suite:        fixarray-vs-glm-highlights
     generated:    2026-08-20
     commit:       2b3a0b8
     host:         pop-os, 20 CPUs @ 4600 MHz
     cpu-scaling:  enabled
     build:        Clang 18.1.3 (1ubuntu1), Google Benchmark v1.9.5
     competitors:  Eigen 3.4.0, GLM GLM: version 0.9.9.8, OpenSSL 3.0.13 30 Jan 2024
     harness:      reps=9, min_time=0.3s, random-interleaving=on
     statistic:    median real time per case; spread = IQR over
                   repetitions, or `sd` where
                   --benchmark_report_aggregates_only hid the raw runs
     publishable:  true
     layout:       highlights
     watch:        stdlib/fixarray/, tests/benchmarks/fixed_glm_bench.cpp

     PRODUCED BY:
       CHEATAH_BENCH_SUITE='fixarray-vs-glm-highlights' \
           CHEATAH_BENCH_LAYOUT='highlights' \
           CHEATAH_BENCH_ROWS='BM_identity_mat4f=mat4f::identity();BM_matmul_mat4f=mat4f * mat4f;BM_add_mat4f=mat4f + mat4f;BM_inverse_mat4d=inverse(mat4d);BM_abs_vec4f=abs(vec4f);BM_dot_vec4f=dot(vec4f, vec4f)' \
           CHEATAH_BENCH_WATCH='stdlib/fixarray/, tests/benchmarks/fixed_glm_bench.cpp' \
           build/release/bin/cheatah_benchmarks --benchmark_filter=^BM_(identity_mat4f|matmul_mat4f|add_mat4f|inverse_mat4d|abs_vec4f|dot_vec4f)_(fixed|glm)$ --benchmark_repetitions=9 --benchmark_min_time=0.3s --benchmark_enable_random_interleaving=true --benchmark_out_format=json --benchmark_out=docs/bench/fixarray-vs-glm-highlights.json --benchmark_format=console
-->

| operation | `Fixed` | GLM | | |
|-----------|--------:|----:|---|---|
| `mat4f::identity()` | **0.66 ns** ±0.01 | 1.78 ns ±0.05 | 2.69× | faster |
| `mat4f * mat4f` | **3.38 ns** ±0.04 | 5.75 ns ±0.14 | 1.70× | faster |
| `mat4f + mat4f` | **0.67 ns** ±0.01 | 1.36 ns ±0.06 | 2.03× | faster |
| `inverse(mat4d)` | **12.15 ns** ±0.31 | 16.83 ns ±0.36 | 1.38× | faster |
| `abs(vec4f)` | **0.44 ns** ±0.01 | 0.82 ns ±0.01 | 1.85× | faster |
| `dot(vec4f, vec4f)` | 0.85 ns ±0.02 | 0.99 ns ±0.02 | 1.17× | parity — gap 0.14 ns |
<!-- BENCH:fixarray-vs-glm-highlights end -->

Read the verdict column, not the ratio. A row can be ahead on ratio and still be called
**parity**, because a difference counts here only when it clears *both* 1.15× and an absolute
0.25 ns — about one cycle. Below that floor the harness's own `DoNotOptimize` scaffolding is a
larger effect than the code being measured, and on 4-element vectors most operations live
there. That floor is also why this tally is smaller than it once was: the earlier
**37 faster / 123 parity** count came from a best-of-N minimum with each library's cases timed
in one consecutive block. Re-measured with interleaved repetitions and medians, a batch of
operations moved from "faster" to "parity". None moved to "slower" — the claim that matters
survived the stricter method.

The [`bench_gate.sh`](../../scripts/bench_gate.sh) hard gate keeps it that way: it fails the
build if any pair regresses past GLM (tolerant by ratio *and* an absolute floor, with a
confirmation re-run so sub-nanosecond noise never flakes it).

Choices that earn it, each a comment in [fixarray.hpp](fixarray.hpp): a matrix is stored
**column-major**, so `m * v` is a sum of contiguous columns rather than horizontal dot
products behind a shuffle network; `dot` sums **pairwise** and, at width ≥ 4, packs the
products into one SIMD multiply (also lowering the rounding error to O(log n)); `normalize`
takes **one reciprocal then multiplies**; `min`/`max`/`abs`/`clamp` are branchless
always-writes that lower to `minps`/`maxps`; and every elementwise op builds its result in
**one pass** through `from_indices` (no default-zero then overwrite). No intrinsics — the
code is shaped so the compiler vectorizes it, no library to link at all.

<!-- BENCH:fixarray-vs-glm begin -->
<!-- cheatah-bench-stamp v1
     suite:        fixarray-vs-glm
     generated:    2026-08-20
     commit:       2b3a0b8
     host:         pop-os, 20 CPUs @ 4600 MHz
     cpu-scaling:  enabled
     build:        Clang 18.1.3 (1ubuntu1), Google Benchmark v1.9.5
     competitors:  Eigen 3.4.0, GLM GLM: version 0.9.9.8, OpenSSL 3.0.13 30 Jan 2024
     harness:      reps=9, min_time=0.3s, random-interleaving=on
     statistic:    median real time per case; spread = IQR over
                   repetitions, or `sd` where
                   --benchmark_report_aggregates_only hid the raw runs
     publishable:  true
     layout:       opstype
     watch:        stdlib/fixarray/, tests/benchmarks/fixed_glm_bench.cpp

     PRODUCED BY:
       CHEATAH_BENCH_SUITE='fixarray-vs-glm' \
           CHEATAH_BENCH_LAYOUT='opstype' \
           CHEATAH_BENCH_WATCH='stdlib/fixarray/, tests/benchmarks/fixed_glm_bench.cpp' \
           build/release/bin/cheatah_benchmarks --benchmark_filter=_(fixed|glm)$ --benchmark_repetitions=9 --benchmark_min_time=0.3s --benchmark_enable_random_interleaving=true --benchmark_out_format=json --benchmark_out=docs/bench/fixarray-vs-glm.json --benchmark_format=console
-->

<details><summary><b>Full GLM comparison — all 160 operations</b> (ns, lower is better; ± is the IQR over 9 interleaved repetitions)</summary>


#### Vectors

| operation | type | `Fixed` | GLM | ratio | |
|---|---|--:|--:|:--:|---|
| `abs` | vec3d | 0.48 ±0.03 | 0.85 ±0.04 | 0.57× | 🟢 faster |
| `abs` | vec3f | 0.46 ±0.03 | 0.83 ±0.04 | 0.56× | 🟢 faster |
| `abs` | vec4d | 0.47 ±0.03 | 0.79 ±0.07 | 0.59× | 🟢 faster |
| `abs` | vec4f | 0.45 ±0.04 | 0.86 ±0.05 | 0.53× | 🟢 faster |
| `add` | vec2d | 0.46 ±0.02 | 0.47 ±0.01 | 0.98× | ⬜ parity |
| `add` | vec2f | 0.35 ±0.05 | 0.23 ±0.01 | 1.53× | ⬜ parity |
| `add` | vec3d | 0.46 ±0.02 | 0.46 ±0.02 | 1.02× | ⬜ parity |
| `add` | vec3f | 0.39 ±0.07 | 0.46 ±0.03 | 0.85× | ⬜ parity |
| `add` | vec4d | 0.46 ±0.02 | 0.48 ±0.03 | 0.98× | ⬜ parity |
| `add` | vec4f | 0.47 ±0.02 | 0.47 ±0.03 | 0.99× | ⬜ parity |
| `clamp` | vec3d | 0.46 ±0.03 | 0.47 ±0.03 | 0.96× | ⬜ parity |
| `clamp` | vec3f | 0.51 ±0.03 | 0.52 ±0.02 | 0.97× | ⬜ parity |
| `clamp` | vec4d | 0.47 ±0.02 | 0.46 ±0.03 | 1.01× | ⬜ parity |
| `clamp` | vec4f | 0.45 ±0.02 | 0.45 ±0.02 | 1.00× | ⬜ parity |
| `cross` | vec3d | 0.71 ±0.04 | 0.70 ±0.06 | 1.01× | ⬜ parity |
| `cross` | vec3f | 0.79 ±0.03 | 0.81 ±0.08 | 0.98× | ⬜ parity |
| `distance2` | vec3d | 0.69 ±0.05 | 0.85 ±0.06 | 0.81× | ⬜ parity |
| `distance2` | vec3f | 0.73 ±0.03 | 0.81 ±0.01 | 0.90× | ⬜ parity |
| `distance2` | vec4d | 1.17 ±0.05 | 1.19 ±0.05 | 0.98× | ⬜ parity |
| `distance2` | vec4f | 1.36 ±0.05 | 1.38 ±0.06 | 0.99× | ⬜ parity |
| `distance` | vec3d | 1.36 ±0.03 | 1.39 ±0.08 | 0.98× | ⬜ parity |
| `distance` | vec3f | 0.82 ±0.00 | 0.90 ±0.03 | 0.92× | ⬜ parity |
| `distance` | vec4d | 1.23 ±0.08 | 1.23 ±0.07 | 1.01× | ⬜ parity |
| `distance` | vec4f | 1.31 ±0.03 | 1.31 ±0.08 | 1.00× | ⬜ parity |
| `divs` | vec2d | 0.45 ±0.03 | 0.47 ±0.02 | 0.97× | ⬜ parity |
| `divs` | vec2f | 0.24 ±0.01 | 0.23 ±0.01 | 1.02× | ⬜ parity |
| `divs` | vec3d | 0.45 ±0.02 | 0.46 ±0.01 | 0.98× | ⬜ parity |
| `divs` | vec3f | 0.47 ±0.01 | 0.45 ±0.02 | 1.03× | ⬜ parity |
| `divs` | vec4d | 0.47 ±0.02 | 0.46 ±0.03 | 1.02× | ⬜ parity |
| `divs` | vec4f | 0.46 ±0.02 | 0.46 ±0.02 | 1.00× | ⬜ parity |
| `dot` | vec2d | 0.47 ±0.01 | 0.46 ±0.01 | 1.03× | ⬜ parity |
| `dot` | vec2f | 0.40 ±0.01 | 0.37 ±0.02 | 1.09× | ⬜ parity |
| `dot` | vec3d | 0.58 ±0.02 | 0.47 ±0.06 | 1.24× | ⬜ parity |
| `dot` | vec3f | 0.59 ±0.03 | 0.54 ±0.02 | 1.10× | ⬜ parity |
| `dot` | vec4d | 0.69 ±0.04 | 0.70 ±0.06 | 0.98× | ⬜ parity |
| `dot` | vec4f | 0.91 ±0.05 | 1.05 ±0.07 | 0.86× | ⬜ parity |
| `len2` | vec2d | 0.46 ±0.02 | 0.46 ±0.01 | 1.00× | ⬜ parity |
| `len2` | vec2f | 0.31 ±0.02 | 0.32 ±0.01 | 0.97× | ⬜ parity |
| `len2` | vec3d | 0.49 ±0.03 | 0.44 ±0.02 | 1.10× | ⬜ parity |
| `len2` | vec3f | 0.47 ±0.03 | 0.46 ±0.03 | 1.01× | ⬜ parity |
| `len2` | vec4d | 0.53 ±0.03 | 0.53 ±0.02 | 1.00× | ⬜ parity |
| `len2` | vec4f | 0.62 ±0.03 | 0.70 ±0.03 | 0.90× | ⬜ parity |
| `len` | vec2d | 1.38 ±0.09 | 1.36 ±0.05 | 1.01× | ⬜ parity |
| `len` | vec2f | 0.68 ±0.04 | 0.68 ±0.02 | 1.00× | ⬜ parity |
| `len` | vec3d | 1.41 ±0.06 | 1.39 ±0.04 | 1.01× | ⬜ parity |
| `len` | vec3f | 0.73 ±0.03 | 0.70 ±0.03 | 1.05× | ⬜ parity |
| `len` | vec4d | 1.36 ±0.04 | 1.41 ±0.03 | 0.96× | ⬜ parity |
| `len` | vec4f | 0.77 ±0.03 | 0.84 ±0.03 | 0.91× | ⬜ parity |
| `max` | vec3d | 0.46 ±0.03 | 0.46 ±0.02 | 1.01× | ⬜ parity |
| `max` | vec3f | 0.40 ±0.06 | 0.39 ±0.06 | 1.03× | ⬜ parity |
| `max` | vec4d | 0.45 ±0.01 | 0.45 ±0.00 | 1.00× | ⬜ parity |
| `max` | vec4f | 0.46 ±0.02 | 0.48 ±0.03 | 0.95× | ⬜ parity |
| `min` | vec3d | 0.47 ±0.01 | 0.47 ±0.02 | 1.02× | ⬜ parity |
| `min` | vec3f | 0.43 ±0.02 | 0.45 ±0.05 | 0.97× | ⬜ parity |
| `min` | vec4d | 0.47 ±0.03 | 0.47 ±0.02 | 1.00× | ⬜ parity |
| `min` | vec4f | 0.46 ±0.02 | 0.47 ±0.03 | 0.98× | ⬜ parity |
| `mix` | vec3d | 0.64 ±0.06 | 0.58 ±0.13 | 1.10× | ⬜ parity |
| `mix` | vec3f | 0.63 ±0.06 | 0.60 ±0.07 | 1.06× | ⬜ parity |
| `mix` | vec4d | 0.59 ±0.03 | 0.69 ±0.06 | 0.85× | ⬜ parity |
| `mix` | vec4f | 0.56 ±0.02 | 0.58 ±0.04 | 0.97× | ⬜ parity |
| `muls` | vec2d | 0.46 ±0.01 | 0.46 ±0.01 | 1.00× | ⬜ parity |
| `muls` | vec2f | 0.25 ±0.02 | 0.23 ±0.01 | 1.08× | ⬜ parity |
| `muls` | vec3d | 0.47 ±0.03 | 0.46 ±0.02 | 1.02× | ⬜ parity |
| `muls` | vec3f | 0.46 ±0.02 | 0.47 ±0.01 | 0.96× | ⬜ parity |
| `muls` | vec4d | 0.48 ±0.05 | 0.46 ±0.01 | 1.04× | ⬜ parity |
| `muls` | vec4f | 0.46 ±0.02 | 0.46 ±0.02 | 1.00× | ⬜ parity |
| `neg` | vec2d | 0.47 ±0.02 | 0.45 ±0.01 | 1.03× | ⬜ parity |
| `neg` | vec2f | 0.23 ±0.01 | 0.24 ±0.01 | 0.99× | ⬜ parity |
| `neg` | vec3d | 0.46 ±0.02 | 0.45 ±0.01 | 1.01× | ⬜ parity |
| `neg` | vec3f | 0.46 ±0.01 | 0.47 ±0.03 | 0.98× | ⬜ parity |
| `neg` | vec4d | 0.48 ±0.03 | 0.45 ±0.02 | 1.07× | ⬜ parity |
| `neg` | vec4f | 0.46 ±0.01 | 0.46 ±0.01 | 1.00× | ⬜ parity |
| `normalize` | vec2d | 2.38 ±0.07 | 2.35 ±0.04 | 1.01× | ⬜ parity |
| `normalize` | vec2f | 1.39 ±0.03 | 1.37 ±0.08 | 1.02× | ⬜ parity |
| `normalize` | vec3d | 2.39 ±0.11 | 2.33 ±0.10 | 1.02× | ⬜ parity |
| `normalize` | vec3f | 1.47 ±0.07 | 1.42 ±0.06 | 1.04× | ⬜ parity |
| `normalize` | vec4d | 2.46 ±0.15 | 2.36 ±0.17 | 1.04× | ⬜ parity |
| `normalize` | vec4f | 1.50 ±0.06 | 1.46 ±0.03 | 1.03× | ⬜ parity |
| `reflect` | vec3d | 2.92 ±0.10 | 2.89 ±0.15 | 1.01× | ⬜ parity |
| `reflect` | vec3f | 2.68 ±0.15 | 2.59 ±0.07 | 1.03× | ⬜ parity |
| `reflect` | vec4d | 3.41 ±0.15 | 3.48 ±0.06 | 0.98× | ⬜ parity |
| `reflect` | vec4f | 3.45 ±0.08 | 3.80 ±0.08 | 0.91× | ⬜ parity |
| `sign` | vec3d | 0.99 ±0.04 | 1.19 ±0.03 | 0.83× | ⬜ parity |
| `sign` | vec3f | 0.92 ±0.05 | 1.28 ±0.03 | 0.72× | 🟢 faster |
| `sign` | vec4d | 0.97 ±0.04 | 1.63 ±0.12 | 0.60× | 🟢 faster |
| `sign` | vec4f | 0.76 ±0.03 | 1.83 ±0.11 | 0.42× | 🟢 faster |
| `smoothstep` | vec3d | 1.33 ±0.02 | 1.16 ±0.05 | 1.15× | ⬜ parity |
| `smoothstep` | vec3f | 1.29 ±0.04 | 1.21 ±0.05 | 1.06× | ⬜ parity |
| `smoothstep` | vec4d | 1.32 ±0.11 | 1.18 ±0.05 | 1.12× | ⬜ parity |
| `smoothstep` | vec4f | 1.41 ±0.06 | 1.28 ±0.05 | 1.10× | ⬜ parity |
| `step` | vec3d | 0.48 ±0.02 | 0.49 ±0.01 | 0.98× | ⬜ parity |
| `step` | vec3f | 0.46 ±0.01 | 0.47 ±0.02 | 0.99× | ⬜ parity |
| `step` | vec4d | 0.47 ±0.03 | 0.47 ±0.05 | 1.00× | ⬜ parity |
| `step` | vec4f | 0.47 ±0.00 | 0.72 ±0.05 | 0.65× | ⬜ parity |
| `sub` | vec2d | 0.47 ±0.02 | 0.46 ±0.01 | 1.02× | ⬜ parity |
| `sub` | vec2f | 0.24 ±0.01 | 0.23 ±0.01 | 1.02× | ⬜ parity |
| `sub` | vec3d | 0.47 ±0.01 | 0.45 ±0.02 | 1.03× | ⬜ parity |
| `sub` | vec3f | 0.43 ±0.04 | 0.38 ±0.08 | 1.14× | ⬜ parity |
| `sub` | vec4d | 0.47 ±0.01 | 0.45 ±0.01 | 1.04× | ⬜ parity |
| `sub` | vec4f | 0.48 ±0.01 | 0.46 ±0.04 | 1.05× | ⬜ parity |

#### Matrices

| operation | type | `Fixed` | GLM | ratio | |
|---|---|--:|--:|:--:|---|
| `add` | mat2d | 0.46 ±0.02 | 0.47 ±0.02 | 0.99× | ⬜ parity |
| `add` | mat2f | 0.46 ±0.02 | 0.45 ±0.01 | 1.03× | ⬜ parity |
| `add` | mat3d | 0.95 ±0.14 | 0.92 ±0.03 | 1.03× | ⬜ parity |
| `add` | mat3f | 0.69 ±0.04 | 1.15 ±0.02 | 0.60× | 🟢 faster |
| `add` | mat4d | 1.60 ±0.19 | 1.48 ±0.10 | 1.08× | ⬜ parity |
| `add` | mat4f | 0.72 ±0.04 | 1.41 ±0.04 | 0.51× | 🟢 faster |
| `compmult` | mat3d | 0.96 ±0.07 | 0.92 ±0.06 | 1.04× | ⬜ parity |
| `compmult` | mat3f | 0.70 ±0.02 | 1.15 ±0.03 | 0.61× | 🟢 faster |
| `compmult` | mat4d | 1.52 ±0.13 | 1.63 ±0.18 | 0.93× | ⬜ parity |
| `compmult` | mat4f | 0.76 ±0.16 | 1.36 ±0.09 | 0.56× | 🟢 faster |
| `det` | mat2d | 0.42 ±0.02 | 0.42 ±0.02 | 1.01× | ⬜ parity |
| `det` | mat2f | 0.46 ±0.02 | 0.46 ±0.02 | 1.01× | ⬜ parity |
| `det` | mat3d | 1.16 ±0.04 | 1.16 ±0.10 | 1.00× | ⬜ parity |
| `det` | mat3f | 1.16 ±0.04 | 1.13 ±0.07 | 1.03× | ⬜ parity |
| `det` | mat4d | 3.83 ±0.12 | 3.75 ±0.20 | 1.02× | ⬜ parity |
| `det` | mat4f | 3.94 ±0.14 | 3.64 ±0.28 | 1.08× | ⬜ parity |
| `identity` | mat2d | 0.46 ±0.02 | 0.46 ±0.01 | 0.99× | ⬜ parity |
| `identity` | mat2f | 0.23 ±0.01 | 0.23 ±0.01 | 0.99× | ⬜ parity |
| `identity` | mat3d | 1.13 ±0.03 | 1.39 ±0.03 | 0.81× | 🟢 faster |
| `identity` | mat3f | 0.92 ±0.02 | 0.91 ±0.02 | 1.01× | ⬜ parity |
| `identity` | mat4d | 1.13 ±0.02 | 2.47 ±0.14 | 0.46× | 🟢 faster |
| `identity` | mat4f | 0.68 ±0.04 | 1.86 ±0.08 | 0.37× | 🟢 faster |
| `inverse` | mat2d | 0.98 ±0.04 | 0.94 ±0.04 | 1.04× | ⬜ parity |
| `inverse` | mat2f | 1.04 ±0.02 | 0.94 ±0.01 | 1.10× | ⬜ parity |
| `inverse` | mat3d | 3.63 ±0.13 | 3.80 ±0.11 | 0.95× | ⬜ parity |
| `inverse` | mat3f | 3.61 ±0.16 | 3.80 ±0.16 | 0.95× | ⬜ parity |
| `inverse` | mat4d | 12.82 ±0.66 | 17.51 ±0.70 | 0.73× | 🟢 faster |
| `inverse` | mat4f | 11.65 ±0.39 | 15.11 ±0.89 | 0.77× | 🟢 faster |
| `invtrans` | mat3d | 4.62 ±0.38 | 4.77 ±0.29 | 0.97× | ⬜ parity |
| `invtrans` | mat3f | 4.50 ±0.36 | 4.06 ±0.16 | 1.11× | ⬜ parity |
| `invtrans` | mat4d | 14.15 ±1.15 | 15.23 ±0.37 | 0.93× | ⬜ parity |
| `invtrans` | mat4f | 11.82 ±1.52 | 14.60 ±0.63 | 0.81× | 🟢 faster |
| `matmul` | mat2d | 0.94 ±0.06 | 0.91 ±0.01 | 1.03× | ⬜ parity |
| `matmul` | mat2f | 0.92 ±0.02 | 0.94 ±0.05 | 0.98× | ⬜ parity |
| `matmul` | mat3d | 3.43 ±0.08 | 3.44 ±0.13 | 1.00× | ⬜ parity |
| `matmul` | mat3f | 3.71 ±0.07 | 3.74 ±0.12 | 0.99× | ⬜ parity |
| `matmul` | mat4d | 5.95 ±0.27 | 6.34 ±0.76 | 0.94× | ⬜ parity |
| `matmul` | mat4f | 3.53 ±0.10 | 6.04 ±0.34 | 0.59× | 🟢 faster |
| `matvec` | mat2d | 0.50 ±0.05 | 0.49 ±0.01 | 1.02× | ⬜ parity |
| `matvec` | mat2f | 0.47 ±0.08 | 0.48 ±0.06 | 0.98× | ⬜ parity |
| `matvec` | mat3d | 1.19 ±0.15 | 1.03 ±0.07 | 1.16× | ⬜ parity |
| `matvec` | mat3f | 1.02 ±0.11 | 1.00 ±0.05 | 1.03× | ⬜ parity |
| `matvec` | mat4d | 1.53 ±0.13 | 1.51 ±0.15 | 1.01× | ⬜ parity |
| `matvec` | mat4f | 1.46 ±0.08 | 1.46 ±0.06 | 1.00× | ⬜ parity |
| `muls` | mat2d | 0.50 ±0.02 | 0.47 ±0.01 | 1.05× | ⬜ parity |
| `muls` | mat2f | 0.47 ±0.05 | 0.46 ±0.03 | 1.02× | ⬜ parity |
| `muls` | mat3d | 0.91 ±0.03 | 0.91 ±0.03 | 1.00× | ⬜ parity |
| `muls` | mat3f | 0.70 ±0.05 | 0.91 ±0.02 | 0.76× | ⬜ parity |
| `muls` | mat4d | 1.18 ±0.02 | 1.19 ±0.07 | 0.99× | ⬜ parity |
| `muls` | mat4f | 0.69 ±0.04 | 1.41 ±0.05 | 0.49× | 🟢 faster |
| `outer` | mat3d | 0.93 ±0.08 | 0.93 ±0.02 | 1.01× | ⬜ parity |
| `outer` | mat3f | 0.95 ±0.08 | 0.94 ±0.05 | 1.01× | ⬜ parity |
| `outer` | mat4d | 1.29 ±0.10 | 1.29 ±0.07 | 1.00× | ⬜ parity |
| `outer` | mat4f | 0.77 ±0.05 | 1.32 ±0.08 | 0.59× | 🟢 faster |
| `transpose` | mat2d | 0.68 ±0.01 | 0.68 ±0.02 | 0.99× | ⬜ parity |
| `transpose` | mat2f | 0.47 ±0.02 | 0.48 ±0.03 | 0.98× | ⬜ parity |
| `transpose` | mat3d | 1.37 ±0.05 | 1.38 ±0.06 | 0.99× | ⬜ parity |
| `transpose` | mat3f | 1.17 ±0.03 | 1.15 ±0.10 | 1.01× | ⬜ parity |
| `transpose` | mat4d | 2.08 ±0.07 | 2.02 ±0.06 | 1.03× | ⬜ parity |
| `transpose` | mat4f | 2.09 ±0.15 | 2.08 ±0.11 | 1.00× | ⬜ parity |

**20 faster, 140 at parity, 0 slower** across 160 operations.

</details>
<!-- BENCH:fixarray-vs-glm end -->
