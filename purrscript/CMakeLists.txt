# ---- cheatah: cheatah's eDSL language ----
#
# A Python-like language (.purr) that compiles to fast native code and is hosted
# by the cheatah runtime. Two parts:
#   * the language TOOLCHAIN (lexer → parser → codegen) — cheatah::compiler
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
# ndarray is added at the root (before linalg, which depends on it).

# cheatah's own unit tests live here (not under the top-level tests/).
if(CHEATAH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
