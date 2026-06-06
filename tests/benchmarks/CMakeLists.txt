# Micro-benchmarks (Google Benchmark) for the same code paths the unit tests
# exercise. One executable; benchmark::benchmark_main supplies main(). Links the
# STATIC library, matching the unit tests.
#
# Build with the `release` preset for meaningful numbers (the default Debug build
# is unoptimized). Run: ./build/release/bin/cheatah_benchmarks
#
# As more primitives land, add a *_bench.cpp here next to its tests.
add_executable(cheatah_benchmarks
    linalg_bench.cpp
)

target_link_libraries(cheatah_benchmarks PRIVATE
    cheatah::linalg_static
    benchmark::benchmark_main
)
