# cheatah `ndarray`

Our own numpy-flavored N-dimensional array, **generic over its element type**,
with full NumPy
[broadcasting](https://numpy.org/doc/stable/user/basics.broadcasting.html).

The array is `basic_ndarray<T>` for any `Field` element type `T` — a real
arithmetic type (int or float family) **or** a `std::complex` of a floating type,
so complex matrices/vectors (and the complex eigenvalues a real matrix can have)
are first-class. The element type is **deduced from the literals** —
`array([1, 2, 3])` is an integer array, `array([1.0, …])` is a `double` array.
`NDArray` is the default `basic_ndarray<double>`. A complex array prints
element-wise Python-style, e.g. `[0+1j, 0-1j]`. (Ordering-dependent ops like
`arange`, and `mean` which returns a `double`, stay real-only.)

Elements live in a shared buffer (`shared_ptr<vector<T>>`); an array is a VIEW
into it — `{shape, strides, offset}` — so reshape and broadcast are zero-copy
(a stretched dimension just gets stride 0). Shared ownership keeps it memory-safe.

Element-wise ops vectorize declaratively: a contiguous fast path uses
`std::transform(std::execution::unseq, …)` (and `sum` uses `std::reduce(unseq)`),
so the compiler emits SIMD for any `T`; broadcast/strided views fall back to a
correct C-order walk.

## Usage

```purr
import io
import ndarray

let a = ndarray.array([1.0, 2.0, 3.0])                  # 1-D
let m = ndarray.array([[1.0, 2.0], [3.0, 4.0]])         # 2-D, shape read off the nesting
let t = ndarray.array([[[1.0], [2.0]], [[3.0], [4.0]]]) # 3-D — nests to any depth
let c = ndarray.add(m, ndarray.scalar(10.0))            # broadcasts the scalar
io.print(ndarray.to_string(c))
```

An `ndarray` is genuinely **N-dimensional**: `array(...)` infers the shape from a nested
list to any rank (a ragged list is rejected, as in numpy), and broadcasting/reductions
work at every rank — `reshape` is the other way to set a shape.

`import ndarray` includes `ndarray.hpp` and links `libcheatah_ndarray`.

## API

### Factories
- `array(values)` — array from a list; a **nested** list builds an N-D array
  (`array([[1,2],[3,4]])` is 2-D), with the shape inferred from the nesting.
- `scalar(value)` — 0-D array (broadcasts to anything).
- `zeros(shape)` / `ones(shape)` / `full(shape, value)` — filled arrays.
- `zeros_like(a)` / `ones_like(a)` / `full_like(a, value)` — filled arrays with
  the same shape and element type as `a` (numpy's `*_like` family).
- `arange(start, stop, step)` — 1-D range, like Python `range`.
- `reshape(a, shape)` — same data, new shape (C-order).

### Broadcasting
- `broadcast_shapes(a, b)` — the NumPy result shape of two shapes.
- `broadcast_to(a, target)` — a zero-copy view of `a` stretched to `target`.

### Element-wise ops (broadcasting)
- `add` / `sub` / `mul` / `divide` — `a` op `b` over the common shape.

Hot-loop notes (all selected automatically, nothing to call): infix `a + b` etc. are
the same functions; an expiring operand's buffer is reused in place (`a + b + c`
allocates once, not twice); and each op also has an allocation-free out-parameter
overload (`add(out, a, b)`) in [ndarray.hpp](ndarray.hpp) for buffer-reuse loops.

### Element-wise math (numpy-style ufuncs)
The array forms of the scalar `math` module: where `math.sqrt(x)` takes one number,
`ndarray.sqrt(a)` applies the function to every element of an array, SIMD-vectorized
on the contiguous fast path:
- `sqrt` / `cbrt` / `exp` / `log` — roots, exponential, natural log.
- `sin` / `cos` / `tan` — trigonometric (radians).
- `abs` — absolute value.

```purr
io.print(ndarray.to_string(ndarray.sqrt(ndarray.array([1.0, 4.0, 9.0]))))   # [1, 2, 3]
```

### Complex
- `complex(re, im)` — build a complex array from real & imaginary parts (broadcasts).
- `real(a)` / `imag(a)` — the real / imaginary parts as a real array.
- `conj(a)` — element-wise complex conjugate (identity on a real array).

```purr
let z = ndarray.complex(ndarray.array([0.0, 2.0]), ndarray.array([1.0, -3.0]))
io.print(ndarray.to_string(z))            # [0+1j, 2-3j]
io.print(ndarray.to_string(ndarray.conj(z)))   # [0-1j, 2+3j]
```

### Reductions, access, display
- `sum(a)` / `mean(a)` — reduce all elements.
- `get(a, index)` — read one element (bounds-checked).
- `shape_of(a)` / `size_of(a)` — query dimensions / element count.
- `to_string(a)` — nested-bracket text, e.g. `"[[1, 2], [3, 4]]"`.

The `NDArray` class exposes `shape()`, `strides()`, `ndim()`, `size()`, `at(index)`,
`buffer()`, and `offset()`.

Negative dims/indices and size-overflowing shapes throw rather than corrupting
memory.

## Performance vs NumPy

The element-wise math ufuncs are benchmarked against NumPy's vectorized equivalents
(same fixed-seed array to both, op run many times, results cross-checked) by
[`scripts/numpy_compare.py`](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/numpy_compare.py).
Each function's **Performance** row above carries its own number; representative results
(µs per op, vs **NumPy 1.26.4** on `x86_64`; your hardware will differ):

| op | operand dimensions | cheatah | NumPy | winner |
|----|--------------------|--------:|------:|--------|
| `ndarray.sqrt` | 64-element array | 0.16 | 0.80 | **cheatah 5.1×** |
| `ndarray.sqrt` | 16384-element array | 13.9 | 13.9 | even |
| `ndarray.exp` | 16384-element array | 11.7 | 46.5 | **cheatah 4.0×** |
| `ndarray.sin` | 16384-element array | 12.1 | 85.4 | **cheatah 7.0×** |
| `ndarray.add` | 16384-element array + scalar | 2.24 | 2.71 | **cheatah 1.2×** |

`exp`/`sin` route their contiguous-`double` case through glibc's **libmvec** vector math
(`_ZGVdN4v_exp`, …) compiled with `-fveclib=libmvec -fno-math-errno` — *without*
`-ffast-math`, so results stay strictly IEEE — and so beat NumPy ≈4–7× at 16384 elements.
`sqrt` is memory-bandwidth-bound (wins small, ties large). The plain element-wise ops like
`add` are bandwidth-bound too: their result buffer is allocated **uninitialized** (no
throwaway zero-fill before the overwrite), so they read once and write once — matching or
beating NumPy. See the [Performance guide](@ref performance) for the single-core-by-design
rationale.

Per-function docs (parameters, complexity, heap behavior) are in [ndarray.hpp](ndarray.hpp).
Tested in [../tests/ndarray_test.cpp](../tests/ndarray_test.cpp); ASan + Valgrind
clean via the QA gate (`security/run-valgrind.sh`).
