# previously_broken — regression tests for bugs that ONCE broke the compiler/runtime.
# Built as its OWN labeled CTest target ("previously_broken") so the QA gate can run these
# FIRST and fail fast on a reintroduced regression (scripts/qa_gate.sh). It reuses the
# purrc→cheatah compile-run harness from tests/purrc.
add_executable(cheatah_previously_broken_tests previously_broken_test.cpp)
target_link_libraries(cheatah_previously_broken_tests PRIVATE GTest::gmock_main)
target_include_directories(cheatah_previously_broken_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../purrc)  # the shared e2e_harness.hpp

target_compile_definitions(cheatah_previously_broken_tests PRIVATE
    PURRC_PATH="$<TARGET_FILE:purrc>"
    CHEATAH_RUNTIME_PATH="$<TARGET_FILE:cheatah>"
    PURR_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}"
    PREVBROKEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}")  # the checked-in .purr regression programs

add_dependencies(cheatah_previously_broken_tests
    purrc
    cheatah
    cheatah_builtins_static
    cheatah_io_static
    cheatah_ndarray_static
    cheatah_random_static
    cheatah_regex_static
    cheatah_time_static
    # The tls stack: TlsLinksWhen*IsAlsoImported forks purrc, which links these archives
    # directly, so they must EXIST before the test runs.
    cheatah_tls_static
    cheatah_hashlib_static
    cheatah_ed25519_static
    cheatah_x25519_static
    cheatah_aead_static
    cheatah_socket_static
    cheatah_p256_static
    cheatah_p384_static)

# LABELS lets the QA gate select this suite with `ctest -L previously_broken` and run it
# before everything else.
gtest_discover_tests(cheatah_previously_broken_tests
    TEST_PREFIX "previously_broken:"
    PROPERTIES LABELS "previously_broken")
