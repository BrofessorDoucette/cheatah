# memory — header-only ownership/borrow module: Owner<T> / Lease<T, Mode> / Request<Lease>.
#
# The scheduling engine (Owner::rread/rwrite bodies) is NOT implemented yet — real TDD. This
# CMakeLists keeps the module and its tests in the build graph so nothing is ever "left out": the
# module is a header-only INTERFACE target, and the (currently-red) test suites are referenced here,
# gated behind an option that we flip ON as we build the engine.

# Header-only module: no compiled sources. INTERFACE so `import memory` (purrc) resolves the headers,
# and any C++ consumer just links cheatah::memory for the include path + -pthread.
add_library(cheatah_memory INTERFACE)
add_library(cheatah::memory ALIAS cheatah_memory)
target_include_directories(cheatah_memory INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cheatah_memory INTERFACE cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(cheatah_memory INTERFACE Threads::Threads)

# ── in-process C++ test suites (engine implemented → GREEN, run in the gate) ─────────────────
# Native std::thread over a shared Owner: the API contract + the adversarial concurrency "gold" suite
# (deterministic-final results despite nondeterministic interleaving). ON by default; turn OFF to skip
# the (slower) concurrency stress:  cmake --preset debug -DCHEATAH_BUILD_MEMORY_TESTS=OFF. See README.md.
option(CHEATAH_BUILD_MEMORY_TESTS "Build the memory module's in-process C++ tests (engine is implemented)" ON)

if(CHEATAH_BUILD_MEMORY_TESTS AND TARGET GTest::gmock_main)
    include(GoogleTest)  # provides gtest_discover_tests in this directory scope
    add_executable(cheatah_memory_tests
        tests/memory_test.cpp                # spec / API contract (compile-time asserts + single-thread)
        tests/memory_concurrency_test.cpp    # GOLD adversarial concurrency (native std::thread)
    )
    target_link_libraries(cheatah_memory_tests PRIVATE
        GTest::gmock_main cheatah::memory cheatah::ndarray_static Threads::Threads)
    add_dependencies(cheatah_memory_tests cheatah_ndarray_static)
    # RUN_SERIAL: the adversarial concurrency suite asserts that read leases actually
    # COEXIST in time (e.g. ManyReadersCoexistThenAWriteDrains: EXPECT_GT(peak, 1)). That
    # only holds when its threads run simultaneously — under a parallel ctest sweep (-j
    # nproc) the box is oversubscribed and the scheduler serializes the readers, so `peak`
    # stays 1 and the test flakes. RUN_SERIAL makes ctest run these alone (no other test
    # concurrent), giving them the cores their timing assumption needs. The suite is small,
    # so the parallelism lost is negligible. (Thread/random tests are result-deterministic,
    # not timing-based, so they need no such guard.)
    gtest_discover_tests(cheatah_memory_tests TEST_PREFIX "memory:"
        PROPERTIES RUN_SERIAL TRUE)
endif()

# NOTE: the cheatah .purr SURFACE tests (stdlib/memory/tests/cheatah_*.purr) also run in the gate, via
# tests/purrc/CMakeLists.txt (suite "memory-cheatah:") — real GREEN output checks over the compiler +
# runtime. The C++ suites above are in-process (they also drive coverage of the headers).
