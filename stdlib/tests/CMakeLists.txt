# cheatah unit tests, co-located with the language. Covers the lexer and the
# io / os standard-library modules.
#
# Linking the io/os module libs here just EXERCISES them; a real cheatah
# executable links only the modules it imports.
include(GoogleTest)

add_executable(cheatah_tests
    lexer_test.cpp
    parser_test.cpp
    codegen_test.cpp
    io_test.cpp
    os_test.cpp
    string_test.cpp
    builtins_test.cpp
    math_test.cpp
    time_test.cpp
    datetime_test.cpp
    random_test.cpp
    statistics_test.cpp
    hashlib_test.cpp
    hashlib_openssl_test.cpp
    crypto_platform_test.cpp
    ed25519_test.cpp
    x25519_test.cpp
    aead_test.cpp
    tls_test.cpp
    rsa_verify_test.cpp
    x509_test.cpp
    ndarray_test.cpp
    linalg_routines_test.cpp
    linalg_smoke_test.cpp
    socket_test.cpp
    p256_test.cpp
    websocket_test.cpp
    requests_test.cpp
    parsers_json_test.cpp
    parsers_xml_test.cpp
    thread_test.cpp
    # parsers/xml/xml.cpp is compiled DIRECTLY into the test binary (like p256.cpp below) so the
    # slab-DOM XML reader's coverage is measured by parsers_xml_test.cpp here, rather than only
    # through the compiled libcheatah_parsers archive. (It is header-clean — no white-box seam.)
    ${CMAKE_SOURCE_DIR}/stdlib/parsers/xml/xml.cpp
    # p256.cpp is compiled DIRECTLY into the test binary (not linked from libcheatah_p256) so the
    # CHEATAH_P256_TESTING white-box seam lives ONLY in this executable, never in any shipped
    # library. The static archive cheatah::p256_static (clean) is therefore NOT linked below; the
    # p256 symbols tls/etc. need are satisfied by this object.
    ${CMAKE_SOURCE_DIR}/stdlib/p256/p256.cpp
    # websocket.cpp is compiled DIRECTLY too, for the same reason: its CHEATAH_WEBSOCKET_TESTING
    # seam (testonly::session_from_bytes — a frame-parser fuzzing hook) lives ONLY here, never in
    # the shipped libcheatah_websocket. cheatah::websocket_static is therefore NOT linked below.
    ${CMAKE_SOURCE_DIR}/stdlib/websocket/websocket.cpp
)

target_link_libraries(cheatah_tests PRIVATE
    cheatah::compiler
    cheatah::io_static
    cheatah::os_static
    cheatah::string_static
    cheatah::builtins_static
    cheatah::math_static
    cheatah::time_static
    cheatah::datetime_static
    cheatah::random_static
    cheatah::statistics_static
    cheatah::hashlib_static
    cheatah::ed25519_static
    cheatah::x25519_static
    cheatah::aead_static
    cheatah::tls_static
    cheatah::socket_static
    cheatah::thread_static
    cheatah::ndarray_static
    cheatah::linalg_static
    GTest::gmock_main
)

# requests is a pure-cheatah (header-only, TRANSPARENT) module: requests.hpp is generated next to
# requests.purr and #included directly by requests_test.cpp. It USES only the header-only surface of
# parsers (parsers::url::Parser + the json schema/read templates), never the compiled JSON DOM parser
# (json.cpp), so we add the parsers include path but do NOT link parsers_static — keeping json.cpp out
# of this in-process denominator. parsers_json_test.cpp covers the header-only JSON surface directly
# (the scan/pool_builder/simd/token headers + the read<T> struct reader), all header-only.
target_include_directories(cheatah_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/stdlib/requests
    ${CMAKE_SOURCE_DIR}/stdlib/parsers
    ${CMAKE_SOURCE_DIR}/stdlib/p256    # p256.hpp, now that p256.cpp is a direct test source
    ${CMAKE_SOURCE_DIR}/stdlib/websocket)  # websocket.hpp, now that websocket.cpp is a direct source
add_dependencies(cheatah_tests cheatah_requests_module)

# The p256 white-box seam (p256.cpp's `#ifdef CHEATAH_P256_TESTING` testonly:: functions) is
# compiled into THIS TEST BINARY ONLY — p256.cpp is a direct source above and the define is set
# here. No libcheatah_p256 ever carries it (see stdlib/p256/CMakeLists.txt), so nothing test-only
# can reach a release. The seam reaches two ~2^-128-measure defensive branches real inputs cannot.
target_compile_definitions(cheatah_tests PRIVATE CHEATAH_P256_TESTING)
# The websocket frame-parser fuzzing seam (websocket.cpp's `#ifdef CHEATAH_WEBSOCKET_TESTING`
# testonly::session_from_bytes) is likewise compiled into THIS TEST BINARY ONLY — websocket.cpp
# is a direct source above, cheatah::websocket_static (clean) is not linked, so no shipped
# library carries it. It lets websocket_test.cpp drive recv() with hostile frames offline.
target_compile_definitions(cheatah_tests PRIVATE CHEATAH_WEBSOCKET_TESTING)

gtest_discover_tests(cheatah_tests TEST_PREFIX "cheatah:")
