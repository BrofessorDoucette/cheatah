# ---- cheatah: the language implementation ----
#
# A Python-like language (.purr) that compiles to fast native code and is hosted
# by the cheatah runtime. Three parts:
#   * the language TOOLCHAIN (lexer → parser → codegen) — cheatah::compiler
#   * the COMPILER executable `purrc` (purrc.cpp) that drives the toolchain
#   * the standard-library MODULES (io, os, …) — one dual static+shared lib each,
#     linked into a cheatah executable ONLY when it `import`s them.

# Language toolchain. Stage: lexer only (see README.md). Pure/dependency-free so
# it unit-tests in isolation.
add_library(cheatah_compiler STATIC
    lexer.cpp
    parser.cpp
    codegen.cpp
)
add_library(cheatah::compiler ALIAS cheatah_compiler)

target_include_directories(cheatah_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cheatah_compiler PUBLIC cxx_std_20)
set_target_properties(cheatah_compiler PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Standard-library modules (each its own static + shared library via the helper).
include(${PROJECT_SOURCE_DIR}/cmake/CheatahLibrary.cmake)
add_subdirectory(builtins)  # always-available built-in functions
add_subdirectory(io)
add_subdirectory(os)
add_subdirectory(string)
add_subdirectory(math)
add_subdirectory(time)
add_subdirectory(datetime)
add_subdirectory(random)
add_subdirectory(statistics)
add_subdirectory(hashlib)
# ndarray + linalg are added at the root (before this dir) so linalg sees ndarray.

# ---- purrc: the cheatah compiler executable ----
# Drives lexer → parser → codegen (cheatah::compiler), then shells out to the C++
# backend to build a loadable module. The paths it needs (stdlib headers/libs,
# runtime header, the C++ compiler) are injected at configure time.
add_executable(purrc purrc.cpp)
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

# cheatah's own unit tests live here (not under the top-level tests/).
if(CHEATAH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
