# cheatah_purrc_tests — end-to-end test of the compiler + runtime pipeline:
# purrc compiles a .purr to a loadable module, cheatah loads and runs it.
add_executable(cheatah_purrc_tests
    compile_run_test.cpp
    cli_test.cpp                # --help / -h / --version usage for purrc + cheatah
    lang_features_test.cpp
    linalg_lsq_e2e_test.cpp
    # Comprehensive per-module system tests (suite StdlibE2E): each exercises
    # EVERY function of its module in one cohesive program (backs the @systest docs).
    builtins_sys_test.cpp
    io_sys_test.cpp
    os_sys_test.cpp
    string_sys_test.cpp
    math_sys_test.cpp
    ndarray_sys_test.cpp
    linalg_sys_test.cpp
    socket_sys_test.cpp
    time_sys_test.cpp
    datetime_sys_test.cpp
    random_sys_test.cpp
    statistics_sys_test.cpp
    hashlib_sys_test.cpp
    ed25519_sys_test.cpp
    # Per-function compile-run tests (one file per stdlib module).
    builtins_cr_test.cpp
    io_cr_test.cpp
    os_cr_test.cpp
    string_cr_test.cpp
    math_cr_test.cpp
    ndarray_cr_test.cpp
    linalg_cr_test.cpp
    socket_cr_test.cpp
    time_cr_test.cpp
    datetime_cr_test.cpp
    random_cr_test.cpp
    statistics_cr_test.cpp
    hashlib_cr_test.cpp
    ed25519_cr_test.cpp
    # System-level tests: real multi-module mini-apps (suite SystemApps).
    app_grades_test.cpp
    app_matrix_test.cpp
    app_eventlog_test.cpp
    app_integrity_test.cpp
    app_montecarlo_test.cpp
    app_netcat_test.cpp
    # Namespace aliasing of the generated C++ (suite NamespaceAliasing): proves the
    # alias is emitted when safe and skipped on a name collision — both compile & run.
    namespace_alias_test.cpp
    # Module integrity (suite ModuleIntegrity): sign a real .so, then prove the runtime
    # runs a genuine module and REFUSES a corrupted / tampered / untrusted one.
    integrity_e2e_test.cpp
    # cheatah library modules (suite LibraryModule): purrc --emit-library (transparent /
    # opaque), import-time checksum verification, and fail-closed on a tampered module.
    library_module_test.cpp
    # linalg vs NumPy: run checked-in .purr programs and cross-check against python.
    linalg_vs_python_test.cpp
)
target_link_libraries(cheatah_purrc_tests PRIVATE GTest::gmock_main)

target_compile_definitions(cheatah_purrc_tests PRIVATE
    PURRC_PATH="$<TARGET_FILE:purrc>"
    CHEATAH_RUNTIME_PATH="$<TARGET_FILE:cheatah>"
    PURR_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}"
    # Source dir holding the editable linalg .purr/.py program pairs.
    LINALG_PROGRAMS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/linalg_programs")

# The test shells out to these at run time, and purrc links the stdlib libs into
# the module it builds — so all must exist before the test runs.
add_dependencies(cheatah_purrc_tests
    purrc
    cheatah
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
    cheatah_ed25519_static
    cheatah_socket_static
    cheatah_ndarray_static
    cheatah_linalg_static)

gtest_discover_tests(cheatah_purrc_tests TEST_PREFIX "purrc:")
