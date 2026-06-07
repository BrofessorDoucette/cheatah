cmake_minimum_required(VERSION 3.24)

# cheatah — a fast, Python-like programming language.
#
# A .purr source file is compiled by `purrc` into a native loadable module and
# run by the headless `cheatah` host. The language transpiles to modern
# C++ and is built at -O3 -march=native, so cheatah programs run at optimized
# native speed. This repository is the language itself: the toolchain (lexer →
# parser → codegen), the compiler `purrc`, the runtime host, and the standard
# library (io/os/string/math/time/datetime/random/statistics/hashlib + the
# numeric core ndarray/linalg).
project(cheatah
    VERSION 0.1.0
    DESCRIPTION "cheatah — a fast, Python-like programming language (.purr)"
    LANGUAGES CXX)

# ---- Standards & global build settings -------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Single, predictable output tree: purrc resolves stdlib archives from <build>/lib
# and the test/benchmark binaries land in <build>/bin.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---- Options ---------------------------------------------------------------
option(CHEATAH_BUILD_TESTS      "Build the unit/integration test suites" ON)
option(CHEATAH_BUILD_BENCHMARKS "Build Google Benchmark micro-benchmarks" OFF)
option(CHEATAH_SANITIZE         "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# ---- Sanitizers (opt-in; applied to everything built here, incl. gtest) -----
# ASan catches memory errors (OOB, use-after-free, leaks); UBSan catches undefined
# behavior. Applied globally and to linking so the runtimes are present. Use the
# `asan` preset (configure+build+test) or -DCHEATAH_SANITIZE=ON. The QA gate runs
# the test suite under these.
if(CHEATAH_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                        -fno-sanitize-recover=all -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# ---- Third-party (test/benchmark only; the language itself has no deps) -----
include(FetchContent)

if(CHEATAH_BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        52eb8108c5bdec04579160ae17225d66034bd723) # pinned
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

if(CHEATAH_BUILD_BENCHMARKS)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        192ef10025eb2c4cdd392bc502f0c852196baa48) # pinned
    FetchContent_MakeAvailable(benchmark)
endif()

# ---- The language ----------------------------------------------------------
# Helper that builds each stdlib module as a dual static+shared library.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheatahLibrary.cmake)

# Numeric core: ndarray must be built before linalg (linalg links it).
add_subdirectory(stdlib/ndarray)
add_subdirectory(stdlib/linalg)

add_subdirectory(compiler)  # language toolchain (cheatah::compiler) + purrc
add_subdirectory(stdlib)    # the standard-library modules + the unit tests
add_subdirectory(runtime)   # the headless host (cheatah)

# purrc links the stdlib module archives into every program it compiles, so they
# must be built whenever purrc is. Wire it here, where purrc (compiler/) and the
# module targets (stdlib/) both exist.
add_dependencies(purrc
    cheatah_builtins_static
    cheatah_io_static
    cheatah_os_static
    cheatah_string_static
    cheatah_math_static
    cheatah_time_static
    cheatah_datetime_static
    cheatah_random_static
    cheatah_statistics_static
    cheatah_hashlib_static
    cheatah_ndarray_static
    cheatah_linalg_static)

if(CHEATAH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
