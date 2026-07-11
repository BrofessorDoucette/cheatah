# cheatah_purrc_tests — end-to-end test of the compiler + runtime pipeline:
# purrc compiles a .purr to a loadable module, cheatah loads and runs it.
add_executable(cheatah_purrc_tests
    compile_run_test.cpp
    cli_test.cpp                # --help / -h / --version usage for purrc + cheatah
    lang_features_test.cpp
    sized_int_cr_test.cpp      # opt-in sized integer storage types (i8..u64) — memory footprint
    fixarray_cr_test.cpp       # fixarray Fixed/vec/mat called from cheatah (construct, typed, ops)
    parsers_cr_test.cpp
    kwargs_cr_test.cpp         # default + keyword arguments (language feature)
    callback_sys_test.cpp      # function values: a `fn` binds to a C++ std::function (callbacks)
    requests_sys_test.cpp      # requests — the first pure-cheatah module, over real TCP
    tls_sys_test.cpp           # TLS 1.3 client vs openssl s_server (test infra only)        # parsers module + dotted type imports + typed-reader schema synthesis
    websocket_sys_test.cpp     # RFC 6455 client vs a real Node `ws` wss echo server (test infra only)
    linalg_lsq_e2e_test.cpp
    regex_e2e_test.cpp         # regex — adversarial end-to-end suite (ReDoS-safe, anchors, classes)
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
    thread_sys_test.cpp        # thread — spawn/join/with-guard/retry + Owner-param lowering
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
    thread_cr_test.cpp
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
    # folded sibling C++ (suite FoldedBase): a foo.hpp/foo.cpp next to foo.purr is woven into the
    # module's generated .gen.hpp/.gen.cpp, so the .purr calls hand-written C++ by bare name.
    folded_base_test.cpp
    # linalg vs NumPy: run checked-in .purr programs and cross-check against python.
    linalg_vs_python_test.cpp
)
target_link_libraries(cheatah_purrc_tests PRIVATE GTest::gmock_main
    cheatah::socket_static   # the loopback server inside requests_sys_test
    cheatah::tls_static cheatah::p256_static cheatah::aead_static cheatah::x25519_static
    cheatah::ed25519_static cheatah::hashlib_static  # tls_sys_test (tls verify -> p256)
    cheatah::websocket_static cheatah::os_static)    # websocket_sys_test (RFC 6455 client + os.urandom)

target_compile_definitions(cheatah_purrc_tests PRIVATE
    PURRC_PATH="$<TARGET_FILE:purrc>"
    CHEATAH_RUNTIME_PATH="$<TARGET_FILE:cheatah>"
    PURR_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}"
    # Source dir holding the editable linalg .purr/.py program pairs.
    LINALG_PROGRAMS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/linalg_programs"
    # websocket_sys_test: the Node `ws` wss echo fixture + the node binary.
    WEBSOCKET_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
    NODE_EXECUTABLE="node")

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
    cheatah_linalg_static
    cheatah_regex_static       # regex_e2e_test (purrc links libcheatah_regex.a into the module)
    cheatah_thread_static      # thread_cr_test / thread_sys_test
    cheatah_websocket_static)  # websocket_sys_test

gtest_discover_tests(cheatah_purrc_tests TEST_PREFIX "purrc:")

# ── memory: cheatah-surface suite (real cheatah .purr programs, compiled + run — GREEN) ──────
# Real cheatah programs (stdlib/memory/tests/cheatah_*.purr) exercising the memory surface end-to-end
# (own / rread / rwrite / acquire / read / write(value|index,value)). The Owner engine is implemented,
# so these are real output checks that run in the gate.
add_executable(cheatah_memory_e2e_tests memory_cheatah_e2e_test.cpp)
target_link_libraries(cheatah_memory_e2e_tests PRIVATE GTest::gmock_main)
target_include_directories(cheatah_memory_e2e_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(cheatah_memory_e2e_tests PRIVATE
    PURRC_PATH="$<TARGET_FILE:purrc>"
    CHEATAH_RUNTIME_PATH="$<TARGET_FILE:cheatah>"
    PURR_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}"
    MEMORY_PURR_DIR="${CMAKE_SOURCE_DIR}/stdlib/memory/tests")
add_dependencies(cheatah_memory_e2e_tests purrc cheatah
    cheatah_ndarray_static cheatah_thread_static)  # header-only memory; thread lib for concurrent .purr
gtest_discover_tests(cheatah_memory_e2e_tests TEST_PREFIX "memory-cheatah:")
