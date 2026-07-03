# cheatah `random`

Pseudo-random numbers from a seedable Mersenne Twister (`std::mt19937_64`); `gauss`
gives normal deviates for Monte Carlo. (For cryptographic randomness use
`os.urandom` / `ed25519`, not this module.)

The engine is **per-thread**: concurrent draws from `thread.spawn`ed workers never
race, and each thread's stream is independent. A thread self-seeds from
`std::random_device` on first use; `seed(s)` seeds the **calling thread's** engine
only — a worker that wants a reproducible stream calls `seed` itself.

```purr
import random

random.seed(42)          # reproducible stream
x = random.random()      # double in [0, 1)
d = random.randint(1, 6) # dice roll
pick = random.choice([1, 2, 3, 4, 5])
```

## Functions

- `seed(s)` — seed the calling thread's engine, making its stream reproducible.
- `random()` — uniform double in [0, 1).
- `uniform(a, b)` — uniform double in [a, b].
- `randint(a, b)` — uniform integer in [a, b] (inclusive).
- `gauss(mu, sigma)` — normal (Gaussian) deviate.
- `choice(seq)` — a random element of a random-access sequence (list/array).

Per-function docs (parameters, runtime complexity, heap behavior) are in
[random.hpp](random.hpp). Tested in
[../tests/random_test.cpp](../tests/random_test.cpp); ASan + Valgrind clean via
the QA gate (`security/run-valgrind.sh`).
