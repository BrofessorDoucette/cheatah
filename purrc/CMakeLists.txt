# ---- purrc: the purrscript compiler ----
#
# Drives lexer → parser → codegen (cheatah::purrscript), then shells out to the
# C++ backend to build a loadable module. The paths it needs (stdlib headers/libs,
# runtime header, the C++ compiler) are injected at configure time.
add_executable(purrc main.cpp)
target_link_libraries(purrc PRIVATE cheatah::purrscript)

target_compile_definitions(purrc PRIVATE
    PURRSCRIPT_ROOT="${PROJECT_SOURCE_DIR}/purrscript"
    PURRSCRIPT_RUNTIME_INCLUDE="${PROJECT_SOURCE_DIR}/runtime"
    PURRSCRIPT_LIB_DIR="${CMAKE_BINARY_DIR}/lib"
    PURRSCRIPT_CXX="${CMAKE_CXX_COMPILER}")

# purrc links these stdlib module libraries into the programs it compiles, so they
# must exist whenever purrc is used.
add_dependencies(purrc
    cheatah_purrscript_builtins_static
    cheatah_purrscript_io_static
    cheatah_purrscript_os_static
    cheatah_purrscript_string_static
    cheatah_purrscript_math_static
    cheatah_purrscript_time_static
    cheatah_purrscript_datetime_static
    cheatah_purrscript_random_static
    cheatah_purrscript_statistics_static
    cheatah_purrscript_hashlib_static
    cheatah_purrscript_ndarray_static
    cheatah_purrscript_linalg_static)
