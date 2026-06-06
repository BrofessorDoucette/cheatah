# purrscript unit tests, co-located with the language. Covers the lexer and the
# io / os standard-library modules.
#
# Linking the io/os module libs here just EXERCISES them; a real purrscript
# executable links only the modules it imports.
include(GoogleTest)

add_executable(cheatah_purrscript_tests
    lexer_test.cpp
    parser_test.cpp
    codegen_test.cpp
    io_test.cpp
    os_test.cpp
    string_test.cpp
    builtins_test.cpp
    math_test.cpp
    time_test.cpp
    modules2_test.cpp
    ndarray_test.cpp
    linalg_routines_test.cpp
)

target_link_libraries(cheatah_purrscript_tests PRIVATE
    cheatah::purrscript
    cheatah::purrscript_io_static
    cheatah::purrscript_os_static
    cheatah::purrscript_string_static
    cheatah::purrscript_builtins_static
    cheatah::purrscript_math_static
    cheatah::purrscript_time_static
    cheatah::purrscript_datetime_static
    cheatah::purrscript_random_static
    cheatah::purrscript_statistics_static
    cheatah::purrscript_hashlib_static
    cheatah::purrscript_ndarray_static
    cheatah::purrscript_linalg_static
    GTest::gmock_main
)

gtest_discover_tests(cheatah_purrscript_tests TEST_PREFIX "purrscript:")
