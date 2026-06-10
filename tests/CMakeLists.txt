# Test suites. The in-process unit tests — including the linear-algebra core and
# its smoke tests — live in stdlib/tests (cheatah_tests). Here we add the
# end-to-end suites:
#   purrc/   -> purrc → cheatah compile-and-run pipeline + per-function + system tests
#
# gtest_discover_tests() (used by the subdir) registers every TEST case as its own
# CTest entry, prefixed per component. GoogleTest is included once here; the
# function it defines is global, so the subdirectory can call it.
include(GoogleTest)

# Regression suite for bugs that ONCE broke the toolchain; the QA gate runs it FIRST
# (CTest label "previously_broken"). Added before the main pipeline suite for tidiness.
add_subdirectory(previously_broken)

add_subdirectory(purrc)  # purrc → cheatah end-to-end pipeline test

# Micro-benchmarks (Google Benchmark) for the same code the unit tests exercise.
if(CHEATAH_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
