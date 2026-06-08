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

let a = ndarray.array([1.0, 2.0, 3.0])
let b = ndarray.reshape(ndarray.arange(0.0, 6.0, 1.0), [2, 3])
let c = ndarray.add(b, ndarray.scalar(10.0))   # broadcasts the scalar
io.print(ndarray.to_string(c))
```

`import ndarray` includes `ndarray.hpp` and links `libcheatah_ndarray`.

## API

### Factories
- `array(values)` — 1-D array from a list.
- `scalar(value)` — 0-D array (broadcasts to anything).
- `zeros(shape)` / `ones(shape)` / `full(shape, value)` — filled arrays.
- `arange(start, stop, step)` — 1-D range, like Python `range`.
- `reshape(a, shape)` — same data, new shape (C-order).

### Broadcasting
- `broadcast_shapes(a, b)` — the NumPy result shape of two shapes.
- `broadcast_to(a, target)` — a zero-copy view of `a` stretched to `target`.

### Element-wise ops (broadcasting)
- `add` / `sub` / `mul` / `divide` — `a` op `b` over the common shape.

### Element-wise math (numpy-style ufuncs)
The array forms of the scalar `math` module — mirroring Python's `math.sqrt(x)` (a
scalar) vs `numpy.sqrt(array)` (a whole array). Each applies the function to every
element, SIMD-vectorized on the contiguous fast path:
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

Per-function docs (parameters, complexity, heap behavior) are in [ndarray.hpp](ndarray.hpp).
Tested in [../tests/ndarray_test.cpp](../tests/ndarray_test.cpp); ASan + Valgrind
clean via the QA gate (`security/run-valgrind.sh`).
