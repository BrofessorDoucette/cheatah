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
    VERSION 1.3.0
    DESCRIPTION "cheatah — a fast, Python-like programming language (.purr)"
    LANGUAGES CXX)

# ---- Standards & global build settings -------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Cross-platform knobs (arch flag, module extension, the flags purrc passes to the C++
# compiler, the math/veclib link). Centralized so Linux/macOS/Windows differ in one file.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Portability.cmake)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Single, predictable output tree: purrc resolves stdlib archives from <build>/lib
# and the test/benchmark binaries land in <build>/bin.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---- Options ---------------------------------------------------------------
# Tests and benchmarks are OFF by default so a plain `cmake -B build` (whether cheatah
# is top-level or pulled in via CPM) builds the compiler + runtime + stdlib with ZERO
# third-party dependencies — GoogleTest, Google Benchmark, and Eigen are fetched ONLY
# when the corresponding option is ON. Developers enable them via the `debug`/`asan`/
# `release` presets (which pin these ON); CI/coverage passes -DCHEATAH_BUILD_TESTS=ON.
option(CHEATAH_BUILD_TESTS      "Build the unit/integration test suites (fetches GoogleTest)" OFF)
option(CHEATAH_BUILD_BENCHMARKS "Build Google Benchmark micro-benchmarks (fetches benchmark+Eigen)" OFF)
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

    # Eigen (header-only) — the reference C++ dense linear-algebra library we bench
    # cheatah's single-threaded numeric core against. It is headers ONLY: download it
    # but do NOT run its CMake project (FetchContent_MakeAvailable would add Eigen's
    # own blas/lapack/test/bench targets to our build and clobber our cache options).
    # The benchmark target just adds ${eigen_SOURCE_DIR} as a system include dir.
    FetchContent_Declare(eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG        3.4.0) # pinned release
    FetchContent_GetProperties(eigen)
    if(NOT eigen_POPULATED)
        FetchContent_Populate(eigen)
    endif()
endif()

# ---- The language ----------------------------------------------------------
# Helper that builds each stdlib module as a dual static+shared library.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheatahLibrary.cmake)

# Numeric core: ndarray must be built before linalg (linalg links it).
add_subdirectory(stdlib/ndarray)
add_subdirectory(stdlib/linalg)

# Crypto modules are added before the compiler + runtime because BOTH link them (purrc
# to sign/checksum, the runtime to verify a module before loading it). ed25519 links
# hashlib, so hashlib comes first.
add_subdirectory(stdlib/hashlib)
add_subdirectory(stdlib/ed25519)

add_subdirectory(compiler)  # language toolchain (cheatah::compiler) + purrc
add_subdirectory(stdlib)    # the standard-library modules + the unit tests
add_subdirectory(runtime)   # the headless host (cheatah)

# `parsers` is C++-authored (templates/variants beyond the .purr subset) — see its README.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheatahModule.cmake)
add_subdirectory(stdlib/parsers)

# Pure-cheatah stdlib modules: authored in .purr and emitted BY purrc (so added AFTER the
# compiler). `requests` — HTTP in pure cheatah — is THE first; transparent (source-in-header),
# verified on import like a C++ module.
add_subdirectory(stdlib/requests)

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
    cheatah_socket_static
    cheatah_sys_static
    cheatah_ndarray_static
    cheatah_linalg_static)

# ---- The package manager (biome) -------------------------------------------
# `biome` is cheatah's package manager — itself a cheatah program compiled to a
# standalone native executable by purrc (--exe). It wraps CPM/CMake so a user
# project pulls in cheatah + optional stdlib extensions declaratively. Added
# after purrc + the stdlib modules exist (it is built BY purrc).
add_subdirectory(pkg-manager)

if(CHEATAH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
