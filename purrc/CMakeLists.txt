# ---- purrc: the cheatah compiler ----
#
# Drives lexer → parser → codegen (cheatah::compiler), then shells out to the
# C++ backend to build a loadable module. The paths it needs (stdlib headers/libs,
# runtime header, the C++ compiler) are injected at configure time.
add_executable(purrc main.cpp)
target_link_libraries(purrc PRIVATE cheatah::compiler)

target_compile_definitions(purrc PRIVATE
    CHEATAH_ROOT="${PROJECT_SOURCE_DIR}/purrscript"
    CHEATAH_RUNTIME_INCLUDE="${PROJECT_SOURCE_DIR}/runtime"
    CHEATAH_LIB_DIR="${CMAKE_BINARY_DIR}/lib"
    CHEATAH_CXX="${CMAKE_CXX_COMPILER}")

# purrc links these stdlib module libraries into the programs it compiles, so they
# must exist whenever purrc is used.
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
    cheatah_ndarray_static
    cheatah_linalg_static)
