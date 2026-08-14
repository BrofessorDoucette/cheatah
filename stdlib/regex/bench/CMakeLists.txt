# Standalone benchmark + differential-test project for `cheatah::regex` vs std::regex vs
# boost::regex vs Google RE2. Boost, RE2, Abseil, GoogleTest and Google Benchmark are
# BENCHMARK-ONLY dependencies: they are fetched + built here (never by the normal cheatah
# build). Configure/build/run:
#     cmake -S stdlib/regex/bench -B build/regexbench -DCMAKE_BUILD_TYPE=Release
#     cmake --build build/regexbench -j
#     ./build/regexbench/rxbench     # 4-engine benchmark + per-rival win/parity/loss tally
#     ./build/regexbench/rxdiff      # differential tests: cheatah vs RE2-as-oracle
# The durable "tie-or-beat RE2" claim is machine-checkable:
#     RXBENCH_ASSERT=1 ./build/regexbench/rxbench \
#         --benchmark_repetitions=7 --benchmark_report_aggregates_only=true
cmake_minimum_required(VERSION 3.24)
project(cheatah_regex_bench CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
# Only build Boost.Regex (and its transitive deps) from the CMake source release.
set(BOOST_INCLUDE_LIBRARIES regex)
set(BOOST_ENABLE_CMAKE ON)
FetchContent_Declare(Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.85.0/boost-1.85.0-cmake.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP ON)

# Google Benchmark + GoogleTest: the same commit pins as the root project's test tree.
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        192ef10025eb2c4cdd392bc502f0c852196baa48) # pinned
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        52eb8108c5bdec04579160ae17225d66034bd723) # pinned

# RE2 requires Abseil. Abseil is declared FIRST with OVERRIDE_FIND_PACKAGE so RE2's
# `find_package(absl)` resolves to this pinned in-tree copy (needs cmake >= 3.24, required above).
set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_ENABLE_INSTALL ON)
FetchContent_Declare(absl
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG        0cf0a5c9d12cc3783363ab20f11613e69fd04c9a # pinned: LTS 20250814.2
    OVERRIDE_FIND_PACKAGE)
set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(re2
    GIT_REPOSITORY https://github.com/google/re2.git
    GIT_TAG        b654eab8f96ef2264105343d56d45d0bf1134231) # pinned: release 2025-11-05
FetchContent_MakeAvailable(Boost benchmark googletest absl re2)

# Deliberately -O3 with no -march=native: no engine gets an ISA the others lack
# (the fixed_glm_bench precedent).
add_executable(rxbench rxbench.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../regex.cpp)
target_include_directories(rxbench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(rxbench PRIVATE -O3)
target_link_libraries(rxbench PRIVATE benchmark::benchmark Boost::regex re2::re2)

# rxdiff needs Boost only because engines.hpp includes it; the tests exercise cheatah vs RE2.
add_executable(rxdiff rxdiff.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../regex.cpp)
target_include_directories(rxdiff PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(rxdiff PRIVATE -O3)
target_link_libraries(rxdiff PRIVATE GTest::gtest_main Boost::regex re2::re2)
