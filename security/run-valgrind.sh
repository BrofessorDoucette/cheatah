#!/usr/bin/env bash
# Run cheatah's in-process unit tests under Valgrind memcheck — a second memory
# checker alongside ASan (each catches things the other can miss). Uses the plain
# `debug` build, because Valgrind cannot run an ASan-instrumented binary.
#
#   security/run-valgrind.sh
#
# Excludes the purrc end-to-end test (it forks the C++ backend and dlopens a
# clang-built .so — noisy and slow under Valgrind); ASan covers that path instead.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

SUPP="security/valgrind.supp"

echo "[valgrind] building (debug)…"
cmake --preset debug          >/tmp/cheatah_vg_cfg.log   2>&1 || { tail -20 /tmp/cheatah_vg_cfg.log;   exit 1; }
cmake --build --preset debug  >/tmp/cheatah_vg_build.log 2>&1 || { tail -30 /tmp/cheatah_vg_build.log; exit 1; }

VG=(valgrind --tool=memcheck --leak-check=full
    --show-leak-kinds=definite,indirect
    --errors-for-leak-kinds=definite,indirect
    --error-exitcode=1 --suppressions="$SUPP")

status=0
for t in cheatah_tests cheatah_linalg_tests; do
    bin="./build/debug/bin/$t"
    [ -x "$bin" ] || { echo "[valgrind] missing $bin"; status=1; continue; }
    echo "[valgrind] memcheck: $t"
    if ! "${VG[@]}" "$bin" >"/tmp/cheatah_vg_$t.log" 2>&1; then
        echo "[valgrind] ERRORS/LEAKS in $t:"; tail -50 "/tmp/cheatah_vg_$t.log"; status=1
    fi
done

if [ "$status" -eq 0 ]; then echo "[valgrind] clean — no errors or definite/indirect leaks"; fi
exit "$status"
