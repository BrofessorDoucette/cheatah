# Test suites — one executable per product target so each component can be built
# and run independently:
#   linalg/  -> cheatah_linalg_tests   : the linear-algebra core (cheatah::linalg)
#   purrc/   -> purrc → cheatah end-to-end compile-and-run pipeline test
#
# gtest_discover_tests() (used by each subdir) registers every TEST/TEST_F/TEST_P
# case as its own CTest entry, prefixed per component (linalg: …) so
# `ctest -R '^linalg:'` runs just that suite. GoogleTest is included once here;
# the function it defines is global, so the subdirectories can call it.
include(GoogleTest)

add_subdirectory(linalg)
add_subdirectory(purrc)  # purrc → cheatah end-to-end pipeline test

# Micro-benchmarks (Google Benchmark) for the same code the unit tests exercise.
if(CHEATAH_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
