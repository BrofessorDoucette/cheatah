# ---- compiler: the cheatah language toolchain + the purrc compiler ----
#
# The frontend (lexer → parser → codegen) and the `purrc` executable that drives
# it. A .purr program is lexed/parsed/codegen'd to C++ here, then purrc shells out
# to the C++ backend to build a loadable module the cheatah runtime runs.

# cheatah::version() — the single source of truth for the version string, built
# from the CMake PROJECT_VERSION. A tiny standalone lib so BOTH purrc and the
# cheatah runtime can report `--version` from the same string, without the runtime
# pulling in the whole toolchain.
add_library(cheatah_version STATIC version.cpp)
add_library(cheatah::version ALIAS cheatah_version)
target_include_directories(cheatah_version PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cheatah_version PUBLIC cxx_std_20)
set_target_properties(cheatah_version PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(cheatah_version PRIVATE CHEATAH_VERSION="${PROJECT_VERSION}")

# Language toolchain: lexer → parser → codegen. Pure/dependency-free so it
# unit-tests in isolation; its public include dir carries token/ast/lexer/parser/
# codegen headers.
add_library(cheatah_compiler STATIC
    lexer.cpp
    parser.cpp
    codegen.cpp
)
add_library(cheatah::compiler ALIAS cheatah_compiler)

target_include_directories(cheatah_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cheatah_compiler PUBLIC cxx_std_20)
set_target_properties(cheatah_compiler PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- purrc: the cheatah compiler executable ----
# Drives the toolchain, then invokes the C++ backend. The paths it needs (stdlib
# module headers/libs, the runtime header, the C++ compiler) are injected here.
# NB: CHEATAH_ROOT points at stdlib/ — purrc resolves `import <m>` to that module's
# header and the libcheatah_<m> archive built from there.
add_executable(purrc purrc.cpp)
target_link_libraries(purrc PRIVATE cheatah::compiler cheatah::version)

target_compile_definitions(purrc PRIVATE
    CHEATAH_ROOT="${PROJECT_SOURCE_DIR}/stdlib"
    CHEATAH_RUNTIME_INCLUDE="${PROJECT_SOURCE_DIR}/runtime"
    CHEATAH_LIB_DIR="${CMAKE_BINARY_DIR}/lib"
    CHEATAH_CXX="${CMAKE_CXX_COMPILER}")

# purrc links the stdlib module archives into the programs it compiles, so those
# must be built whenever purrc is. The module targets live in stdlib/ (added after
# this directory), so the build-order dependency is wired at the root once both
# purrc and the module targets exist.
