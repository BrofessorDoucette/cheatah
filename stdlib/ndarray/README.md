# cheatah `ndarray`

Our own numpy-flavored N-dimensional array, **generic over its numeric element
type**, with full NumPy
[broadcasting](https://numpy.org/doc/stable/user/basics.broadcasting.html).

The array is `basic_ndarray<T>` for any `Numeric` element type `T` (int or float
family); the element type is **deduced from the literals** — `array([1, 2, 3])`
is an integer array, `array([1.0, …])` is a `double` array. `NDArray` is the
default `basic_ndarray<double>`.

Elements live in a shared buffer (`shared_ptr<vector<T>>`); an array is a VIEW
into it — `{shape, strides, offset}` — so reshape and broadcast are zero-copy
(a stretched dimension just gets stride 0). Shared ownership keeps it memory-safe.

Element-wise ops vectorize declaratively: a contiguous fast path uses
`std::transform(std::execution::unseq, …)` (and `sum` uses `std::reduce(unseq)`),
so the compiler emits SIMD for any `T`; broadcast/strided views fall back to a
correct C-order walk.

## Usage

```purr
import ndarray

a = ndarray.array([1.0, 2.0, 3.0])
b = ndarray.reshape(ndarray.arange(0.0, 6.0, 1.0), [2, 3])
c = ndarray.add(b, ndarray.scalar(10.0))   # broadcasts the scalar
print(ndarray.to_string(c))
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
