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
| `cheatah_interleaved.purr` | two owners, data-dependent order, conserved invariant | `50 50` `100` |

**C++ suites** — for what cheatah *can't* express yet (threads need `cpp{}`; the priority/immediate
template-arg call isn't spellable in cheatah), so concurrency lives here (native `std::thread`, and it
also drives in-process coverage):

| file | kind | drives coverage? |
|---|---|---|
| `memory_test.cpp` | compile-time asserts + single-thread contract | yes (in-process) |
| `memory_concurrency_test.cpp` | GOLD adversarial concurrency, native threads | yes (in-process) |

Verified today: the C++ suites **compile cleanly against the header API** (the *only* unresolved
symbols are `Owner::rread` / `Owner::rwrite`), and every `.purr` file **compiles** — so the red bar is
precisely "the engine isn't written yet," not a broken or invalid test.

## How to run them (now, while building the engine — TDD red → green)

```sh
cmake --preset debug -DCHEATAH_BUILD_MEMORY_TESTS=ON
cmake --build build/debug --target cheatah_memory_tests
ctest --test-dir build/debug -R '^memory:' --output-on-failure
# turn back off when done so the default gate isn't red:
cmake --preset debug -DCHEATAH_BUILD_MEMORY_TESTS=OFF
```

Once the engine is implemented, remove the default-OFF and let this suite run in the gate always
(it becomes an in-process suite in `cheatah_tests`/its own target, so it also drives the 100% coverage
gate for `stdlib/memory/*.hpp`). If the concurrency stress ever dominates gate wall-clock, lower
`kIters` in `memory_concurrency_test.cpp` or split the heavy cases into a release-only ctest label —
the plumbing (native threads, deterministic-final assertions) stays identical.

## Prerequisites already handled

- `purrc` header-only resolution fixed (`compiler/purrc.cpp` — baked-stdlib fallback no longer links a
  nonexistent `libcheatah_memory.a`), so `import memory` works header-only.
- `-pthread` declared via `// cheatah-link: -pthread` in `memory.hpp`, so the `.purr` surface programs
  (std::future/thread) link once the engine exists.

## Design note — why these can't be pure `.purr`

Threads need the `cpp { }` escape hatch (cheatah has no native threads yet), and the priority /
immediate-write API is a template-arg call (`o.rwrite<memory::immediate>()`) that plain cheatah can't
spell — so the *heavy* tests are C++ (which also gives coverage), and the `.purr` files prove the
language surface end-to-end (`import memory`, `memory.own`, `request → acquire → lease`, threads via
`cpp {}`).
