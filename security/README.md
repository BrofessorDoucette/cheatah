# security/

Security tooling for cheatah. The policy and threat model live in the top-level
[SECURITY.md](../SECURITY.md); this folder holds the runnable checks.

| File | What |
|------|------|
| `run-valgrind.sh` | Run the in-process unit tests under Valgrind memcheck (a second memory checker alongside ASan). |
| `valgrind.supp` | Valgrind suppressions (kept minimal — confirmed false positives only). |

## Memory checking

cheatah is checked two ways:

- **ASan + UBSan** — `cmake --preset asan && ctest --preset asan` (also run by the
  QA gate, `scripts/qa_gate.sh`). Fast; catches OOB, use-after-free, leaks, and UB.
- **Valgrind memcheck** — `security/run-valgrind.sh`. Slower, instruments the
  unmodified binary, and catches some things ASan doesn't (and vice versa).

Both should be **clean**. Every standard-library function is covered by a unit
test (see `stdlib/tests/`, one file per module) so these checkers actually exercise
the code.
