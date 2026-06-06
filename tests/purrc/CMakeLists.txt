# cheatah_purrc_tests — end-to-end test of the compiler + runtime pipeline:
# purrc compiles a .purr to a loadable module, cheatah-runtime loads and runs it.
add_executable(cheatah_purrc_tests
    compile_run_test.cpp
)
target_link_libraries(cheatah_purrc_tests PRIVATE GTest::gmock_main)

target_compile_definitions(cheatah_purrc_tests PRIVATE
    PURRC_PATH="$<TARGET_FILE:purrc>"
    CHEATAH_RUNTIME_PATH="$<TARGET_FILE:cheatah-runtime>"
    PURR_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}")

# The test shells out to these at run time, and purrc links the stdlib libs into
# the module it builds — so all must exist before the test runs.
add_dependencies(cheatah_purrc_tests
    purrc
    cheatah-runtime
    cheatah_builtins_static
    cheatah_io_static
    cheatah_os_static
    cheatah_string_static
    cheatah_math_static
    cheatah_time_static
    cheatah_datetime_static
    cheatah_random_static
    cheatah_statistics_static
    cheatah_hashlib_static)

gtest_discover_tests(cheatah_purrc_tests TEST_PREFIX "purrc:")
