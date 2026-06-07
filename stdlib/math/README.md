# cheatah `math` 🐆

Scalar math: Python's `math` module plus the math-flavored built-ins
(`abs`/`min`/`max`/`pow`). Every function is **pure and allocation-free** — it
operates on `double` / `long long` by value and never touches the heap.

```python
import math

print(math.sqrt(2.0))        # 1.4142135623730951
print(math.gcd(48, 18))      # 6
print(abs(-3), max(1, 9, 4)) # 3 9
```

## What's here

- **Constants** — `pi`, `e`, `tau`, `inf`, `nan`.
- **Built-ins** — `abs`, `min`, `max` (variadic), `pow`.
- **Roots & rounding** — `sqrt`, `cbrt`, `fabs`, `floor`, `ceil`, `trunc`, `round`.
- **Exp & logs** — `exp`, `log`, `log2`, `log10`.
- **Trigonometry** — `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `hypot`.
- **Float utilities** — `fmod`, `copysign`, `degrees`, `radians`, `isnan`, `isinf`, `isfinite`.
- **Integer** — `gcd` (O(log min)), `factorial` (O(n)).

Per-function docs (parameters, runtime complexity, heap behavior) are in
[math.hpp](math.hpp). Tested in [../tests/math_test.cpp](../tests/math_test.cpp);
ASan + Valgrind clean via the QA gate (`security/run-valgrind.sh`).
