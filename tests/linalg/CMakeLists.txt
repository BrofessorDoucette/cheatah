# cheatah_linalg_tests: unit tests for the linear-algebra core (cheatah::linalg).
#
# Links the STATIC library (validates libcheatah_linalg.a). Together with the
# engine/app linking the shared lib, a normal build exercises BOTH artifacts.
add_executable(cheatah_linalg_tests
    smoke_test.cpp
)

target_link_libraries(cheatah_linalg_tests PRIVATE
    cheatah::purrscript_linalg_static
    GTest::gmock_main
)

# Register each case with CTest, namespaced so `ctest -R '^linalg:'` runs only
# this component's tests.
gtest_discover_tests(cheatah_linalg_tests TEST_PREFIX "linalg:")
