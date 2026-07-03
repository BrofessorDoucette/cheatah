#!/usr/bin/env bash
# check_test_orphans.sh — fail if any test source file is not referenced by a CMakeLists.txt.
#
# "Don't leave tests out." A *_test.cpp that no CMakeLists names is silently never built or run — the
# worst kind of gap, because it looks like coverage that isn't there. This greps every test source's
# basename across all CMakeLists.txt and reports any that are unreferenced.
#
# Exit 0 if every test file is referenced (or referenced under a gated option — that still counts, it
# is discoverable and one flag away), non-zero otherwise. Run from the repo root.
set -euo pipefail
cd "$(dirname "$0")/.."

# All C++ gtest sources anywhere under a tests/ dir or matching *_test.cpp.
mapfile -t test_files < <(git ls-files '*_test.cpp' | sort)

orphans=()
for f in "${test_files[@]}"; do
    base="$(basename "$f")"
    if ! grep -rqF --include=CMakeLists.txt -e "$base" .; then
        orphans+=("$f")
    fi
done

if ((${#orphans[@]})); then
    echo "ERROR: ${#orphans[@]} test file(s) are not referenced by any CMakeLists.txt (they never build/run):"
    printf '  %s\n' "${orphans[@]}"
    echo "Add each to an add_executable(...) source list (a gated option is fine — it's still referenced)."
    exit 1
fi
echo "OK: all ${#test_files[@]} test source files are referenced by a CMakeLists.txt."
