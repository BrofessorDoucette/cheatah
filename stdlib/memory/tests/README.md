# memory — test manifest

The `memory` scheduling engine is **implemented and green**. These suites were written first (real
TDD) and now all pass in the gate: the C++ suites are in-process (`CHEATAH_BUILD_MEMORY_TESTS`, ON by
default) and the cheatah `.purr` surface tests run via `tests/purrc/` (suite `memory-cheatah:`). All
suites are referenced by a `CMakeLists.txt` (checked by `scripts/check_test_orphans.sh`).

## What's here

**Real cheatah tests** (`.purr`, run by `memory_surface_e2e_test.cpp`) — the primary suite. The whole
*sequential* memory surface is expressible in plain cheatah (verified: every file below parses,
codegens, and compiles); these are actual cheatah programs, not C++ hidden in `cpp{}`.

| `.purr` file | what it exercises | expected stdout |
|---|---|---|
| `cheatah_scalar.purr` | scalar owner: write / read / read-modify-write | `5` `7` `12` |
| `cheatah_struct.purr` | struct owner: per-field writes stay consistent | `0 0` `3 4` `7` |
| `cheatah_accumulate.purr` | 1000 sequential leases accumulate exactly | `1000` `0` |
| `cheatah_string.purr` | heap-backed (str) owner that grows/relocates | `5` `hello world` `11` |
| `cheatah_lease_state.purr` | a read lease is `valid()` / not `expired()` | `True` `False` `42` |
| `cheatah_ndarray.purr` | **owner of an ndarray**: element writes, untouched rest | `True` `True` `True` |
| `cheatah_concurrent_sum.purr` | four `thread.spawn` workers share one owner; the total is exact | `20000` |

**C++ suites** — for what cheatah cannot spell: the immediate-write (`rwrite<memory::immediate>()`,
a negative template argument) and the adversarial stress over raw `std::thread` and atomics:

| file | kind |
|---|---|
| `memory_test.cpp` | compile-time asserts + single-thread contract |
| `memory_concurrency_test.cpp` | GOLD adversarial concurrency, native threads |

## How to run them

```sh
cmake --preset debug
cmake --build build/debug --target cheatah_memory_tests
ctest --test-dir build/debug -R '^memory:' --output-on-failure
```

`-DCHEATAH_BUILD_MEMORY_TESTS=OFF` skips the C++ suites for a faster local configure. If the
concurrency stress ever dominates gate wall-clock, lower `kIters` in `memory_concurrency_test.cpp`
or split the heavy cases into a release-only ctest label — the plumbing (native threads,
deterministic-final assertions) stays identical.

## Build facts

- `memory` is header-only (an INTERFACE target in `stdlib/memory/CMakeLists.txt`); `import memory`
  links no archive.
- `memory.hpp` carries `// cheatah-link: -pthread`, so every program that imports it links the
  thread library.

## Why the heavy tests are C++

The immediate-write is a negative template argument (`o.rwrite<memory::immediate>()`) that cheatah
cannot spell, and the adversarial stress wants raw `std::thread` and atomics — so those tests are
C++. The `.purr` files prove the language surface end-to-end (`import memory`, `memory.own`,
`request → acquire → lease`, and `thread.spawn` workers over one shared owner).

## Which instrument finds what — measured, not assumed

Two real flakes lived in this suite and both were found by *accidental* timing perturbation (machine
load; ThreadSanitizer's slowdown). Neither was a module bug — both were tests using `sleep_for` as a
synchronisation primitive and asserting an emergent ordering they never arranged. The lanes below
exist so that perturbation is deliberate and repeatable instead of lucky.

| lane | what it is good for | how to run |
|---|---|---|
| **native + load** | the scheduler you actually ship on; volume buys coverage | `--gtest_repeat=200` while the box is busy |
| **ThreadSanitizer** | **the instrument of record here.** It models C++11 atomics, which is how this module synchronises | `cmake --preset tsan && cmake --build --preset tsan --target cheatah_memory_tests && ./build/tsan/bin/cheatah_memory_tests` |
| **Helgrind / DRD** | **lock-order inversions and deadlocks** — the class no other lane catches | `valgrind --tool=helgrind --fair-sched=yes ./build/debug/bin/cheatah_memory_tests` |
| **jitter** | interleavings the scheduler never picks on its own | `CHEATAH_MEMORY_JITTER=200 ./build/release/bin/cheatah_memory_tests` |

**Helgrind reports data races in this module and they are FALSE POSITIVES.** It does not model
`std::atomic`, and the preempt/resume handshake (`gate_->valid`, `gate_->acked`) is entirely atomic —
so Helgrind cannot see the happens-before edge and flags every access to the owned object. The
evidence that they are spurious is not an opinion: ThreadSanitizer, which *does* model atomics,
reports **zero** races across 20 full-suite repeats, and every Helgrind context resolves to the owned
value on the atomic-guarded path. Run Helgrind for **lock order**, where it is authoritative and where
it currently reports nothing, and read TSan for races.

Two accommodations make the valgrind lane tractable, and both announce themselves at runtime rather
than changing results silently:

- `kIters` drops to 300 under valgrind (`RUNNING_ON_VALGRIND`). At ~100x instrumentation the full
  20,000 does not finish inside any sane timeout — measured, not guessed.
- `MemoryConcurrency.ManyReadersCoexistThenAWriteDrains` **skips** under valgrind. Those tools
  serialize threads, so two read leases can never be live at once and the property is unobservable.
  It is skipped with the reason printed, never weakened to an assertion that passes by testing nothing.

`CHEATAH_MEMORY_JITTER=<max_us>` injects a random 0..max_us delay at lease boundaries.
`CHEATAH_MEMORY_SEED=<n>` replays a run; when unset a seed is drawn and printed, so a random failure
is reproducible. A fuzzer whose failures cannot be replayed is a rumour generator.
