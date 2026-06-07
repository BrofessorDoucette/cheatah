#!/usr/bin/env bash
# Measure cheatah standard-library test coverage with clang source-based coverage
# (the in-process unit tests: cheatah_tests + cheatah_linalg_tests).
#
#   scripts/coverage.sh              # per-file summary report for stdlib/
#   scripts/coverage.sh show <file>  # uncovered lines of one file, e.g. stdlib/linalg/routines.cpp
#   scripts/coverage.sh funcs <file> # per-function coverage of one file
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

B=build/cov
cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCHEATAH_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
  -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate -fcoverage-mapping" >/tmp/cheatah_cov_cfg.log 2>&1 \
  || { tail -15 /tmp/cheatah_cov_cfg.log; exit 1; }
cmake --build "$B" --target cheatah_tests cheatah_linalg_tests >/tmp/cheatah_cov_build.log 2>&1 \
  || { tail -25 /tmp/cheatah_cov_build.log; exit 1; }

( cd "$B"
  LLVM_PROFILE_FILE=t1.profraw ./bin/cheatah_tests        >/dev/null 2>&1
  LLVM_PROFILE_FILE=t2.profraw ./bin/cheatah_linalg_tests >/dev/null 2>&1
  llvm-profdata merge -sparse t1.profraw t2.profraw -o merged.profdata )

OBJS=(./"$B"/bin/cheatah_tests -object ./"$B"/bin/cheatah_linalg_tests)
PROF="-instr-profile=$B/merged.profdata"
SRCS=$(git ls-files 'stdlib/*.cpp' 'stdlib/*.hpp' | grep -v '/tests/')

case "${1:-report}" in
    show)  llvm-cov show   "${OBJS[@]}" $PROF "${2:?usage: coverage.sh show <file>}" 2>/dev/null \
             | grep -nE '\|[[:space:]]*0\|' || echo "all lines covered in ${2}" ;;
    funcs) llvm-cov report "${OBJS[@]}" $PROF -show-functions "${2:?usage: coverage.sh funcs <file>}" 2>/dev/null ;;
    *)     llvm-cov report "${OBJS[@]}" $PROF $SRCS 2>/dev/null ;;
esac
