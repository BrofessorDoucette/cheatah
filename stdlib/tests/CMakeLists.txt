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
    modules2_test.cpp
    ndarray_test.cpp
    linalg_routines_test.cpp
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
    cheatah::ndarray_static
    cheatah::linalg_static
    GTest::gmock_main
)

gtest_discover_tests(cheatah_tests TEST_PREFIX "cheatah:")
