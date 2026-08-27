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

```purr
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
`outerProduct`, `inverseTranspose`), at sizes 2/3/4 — the GLSL common functions and the
extra matrix builtins at 3 and 4 — in both `float` and `double`
([`fixed_glm_bench.cpp`](../../tests/benchmarks/fixed_glm_bench.cpp)). Both sides compile in
the same translation unit with the same flags, so neither gets an instruction set the other
lacks — and the benchmark **verifies the two produce identical results before it times
either**, so a fast wrong answer can never masquerade as a win.

`Fixed` is **faster than or at parity with GLM across the whole overlap, and slower on none**.
The generated tables that carry the exact tally — a representative sample, the complete
comparison, and the reading of the parity floor that decides each verdict — are on the
[fixarray benchmarks](BENCHMARKS.md) page, and the
[`bench_gate.sh`](../../scripts/bench_gate.sh) hard gate keeps the claim true: it fails the
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
