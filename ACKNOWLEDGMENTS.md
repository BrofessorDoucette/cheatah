# Acknowledgments

**cheatah © 2026 [BigBrain LLC](LICENSE) — MIT-licensed.** cheatah was designed and
written by **Joshua Doucette — lead engineer and executive producer — on behalf of
BigBrain LLC**. Every implementation and every design decision is **original work**;
we have done our best, faithfully and in good conscience, to **avoid plagiarism of any
kind** — no source was copied — while gratefully retaining the best ideas of the
open-source giants upon whose shoulders we stand. cheatah is **dependency-free**: it
bundles, vendors, and links **none** of the projects below at runtime. They are
recognized here because their designs, specifications, reference algorithms, and
documentation informed cheatah's syntax and standard library — and as a tip of the hat
to the open-source "rules of the road." Credit for cheatah's own code remains with
BigBrain LLC; credit for the prior art that inspired it is gladly given here.

Each project remains the work and property of its respective authors, under its
own license.

## Standing on the shoulders of

| Project | Link | How it informed cheatah |
|---|---|---|
| **Python** | <https://www.python.org/> | The language cheatah's surface syntax is modeled on — "Python for people who care about performance." |
| **C++ Standards Committee (ISO WG21)** | <https://isocpp.org/std/the-committee> | cheatah is an embedded C++ DSL; it transpiles to standard C++20 and its STL. |
| **cppreference** | <https://en.cppreference.com/> | The C++ language/library reference relied on throughout the toolchain and stdlib. |
| **NumPy** | <https://numpy.org/> | The API surface and semantics of the `ndarray` numeric core and ufuncs. |
| **SciPy** | <https://scipy.org/> | The shape and naming of the `linalg` / `statistics` routines. |
| **Matplotlib** | <https://matplotlib.org/> | The charting model for the planned `cheatah-plot` extension. |
| **BLAS** | <https://www.netlib.org/blas/> | Reference conventions and algorithms for dense linear algebra. |
| **LAPACK** | <https://www.netlib.org/lapack/> | Reference factorizations and solver semantics behind `linalg`. |
| **Eigen** | <https://eigen.tuxfamily.org/> | The C++ reference library cheatah benchmarks its single-threaded numeric core against. |
| **GLM** | <https://github.com/g-truc/glm> | Vector/matrix and graphics-math conventions (relevant to `cheatah-space` / `cheatah-plot`). |
| **RE2** | <https://github.com/google/re2> | The linear-time, backtracking-free **lazy-DFA** approach behind the `regex` module, plus the published ideas its matcher borrows — the reversed-program pass for `$`-anchored searches, start-state-armed prefix acceleration, and the simultaneous front+back literal probe (designed and written from scratch; no RE2 code). Also the reference engine the standalone `stdlib/regex/bench/` comparison fetches and races against. |
| **Abseil** | <https://github.com/abseil/abseil-cpp> | RE2's required base library — fetched only by the standalone regex benchmark project, never linked into cheatah. |
| **Boost.Regex** | <https://www.boost.org/libs/regex> | The backtracking reference engine in the same standalone regex benchmark (fetched there only; never part of the cheatah build). |
| **IETF RFCs** | <https://www.rfc-editor.org/> | The wire specifications re-implemented from their text: TLS 1.3 (8446), ChaCha20-Poly1305 (8439), X25519 (7748), Ed25519 (8032), WebSocket (6455). |

…and the broader open-source numerical and systems-programming community whose
work makes a project like this possible.

> No source code from the projects above has been copied into cheatah. Where a
> reference text informed an implementation, the algorithm was re-derived and
> re-implemented from its description rather than transcribed.
