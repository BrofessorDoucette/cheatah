# cheatah-example — a template cheatah standard-library extension, authored in pure cheatah.
#
# An extension repo pulls in the cheatah toolchain (for purrc + the cmake helpers) and emits
# its module(s) the same way the in-tree pure-cheatah stdlib modules (e.g. `parsers`) are
# built — from a `.purr` via `purrc --emit-library`. A consuming project fetches THIS repo
# via CPM (`biome add cheatah-example`); its purrc then verifies and links the module.
cmake_minimum_required(VERSION 3.24)
project(cheatah-example LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake OPTIONAL RESULT_VARIABLE _have_cpm)
if(NOT _have_cpm)
    # Standalone builds bootstrap CPM; when fetched BY a project, the parent already has it.
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CPM.cmake OPTIONAL)
endif()

# The toolchain provides purrc and cmake/CheatahModule.cmake (cheatah_add_module). A real
# extension pins the same cheatah version it targets.
CPMAddPackage(NAME cheatah GITHUB_REPOSITORY BrofessorDoucette/cheatah GIT_TAG v1.1.1)
include(${cheatah_SOURCE_DIR}/cmake/CheatahModule.cmake)

# Emit the module from its .purr (run BY purrc). OPAQUE by default — ships the public API in
# example/example.hpp and hides concretely-typed implementations in a signed
# libcheatah_example.a. Add TRANSPARENT to instead inline the generated C++ source into the
# committed header (what the first-party standard library does). `import example` finds and
# verifies example/example.hpp; the program's purrc links the archive when one is present.
cheatah_add_module(example SOURCES example/example.purr)
