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
    ndarray_bench.cpp
    # Native C++ comparisons: cheatah's numeric core vs the reference libraries.
    eigen_compare_bench.cpp   # vs Eigen — dynamic dense linear algebra
    glm_compare_bench.cpp     # vs GLM  — small fixed-size vector/matrix math
)

target_link_libraries(cheatah_benchmarks PRIVATE
    cheatah::linalg_static
    cheatah::ndarray_static
    benchmark::benchmark_main
)

# Eigen (header-only, fetched in the top-level CMakeLists) and GLM (system headers).
if(DEFINED eigen_SOURCE_DIR)
    target_include_directories(cheatah_benchmarks SYSTEM PRIVATE ${eigen_SOURCE_DIR})
endif()
# GLM resolves from the system include path (/usr/include/glm) as <glm/glm.hpp>.
