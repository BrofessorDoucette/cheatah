# cheatah `statistics`

Descriptive statistics over numeric sequences, mirroring Python's `statistics`
module.

```purr
import statistics

statistics.mean([2, 4, 4, 4, 5, 5, 7, 9])   # 5.0
statistics.median([3, 1, 2])                 # 2.0
statistics.stdev([1, 2, 3, 4, 5])            # sample std-dev
```

This is a **header-only** module: every function is a template constrained by the
`NumericRange` concept, so it accepts any iterable of arithmetic values
(`list[float]`, `array[int]`, …).

## Functions

Reductions:
- `sum(data)` — sum of the elements (as `double`).
- `count(data)` — number of elements.
- `mean(data)` — arithmetic mean (0.0 if empty).

Spread:
- `pvariance(data)` / `pstdev(data)` — population variance / std-dev (÷ N).
- `variance(data)` / `stdev(data)` — sample variance / std-dev (÷ N−1).

Order statistics:
- `median(data)` — middle value, or the mean of the two middle values.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[statistics.hpp](statistics.hpp). Tested in
[../tests/statistics_test.cpp](../tests/statistics_test.cpp); ASan + Valgrind
clean via the QA gate (`security/run-valgrind.sh`).
