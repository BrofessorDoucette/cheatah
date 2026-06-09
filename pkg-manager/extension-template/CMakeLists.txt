# cheatah-example — a template cheatah standard-library extension.
#
# An extension repo pulls in the cheatah toolchain (for the add_cheatah_library
# helper and the build settings) and builds its module(s) the same way the in-tree
# stdlib modules are built. A consuming project fetches THIS repo via CPM
# (`biome add cheatah-example`).
cmake_minimum_required(VERSION 3.24)
project(cheatah-example LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake OPTIONAL RESULT_VARIABLE _have_cpm)
if(NOT _have_cpm)
    # Standalone builds bootstrap CPM; when fetched BY a project, the parent already has it.
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CPM.cmake OPTIONAL)
endif()

# The toolchain provides cmake/CheatahLibrary.cmake (add_cheatah_library) and the
# portability knobs. A real extension pins the same cheatah version its module targets.
CPMAddPackage(NAME cheatah GITHUB_REPOSITORY BrofessorDoucette/cheatah GIT_TAG v0.9.0)
include(${cheatah_SOURCE_DIR}/cmake/CheatahLibrary.cmake)

# Build the module: produces libcheatah_example.{a,so}; `import example` links it.
add_cheatah_library(example SOURCES example/example.cpp)
target_include_directories(cheatah_example_obj PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/example)
