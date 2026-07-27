# add_cheatah_library(<name> SOURCES <a.cpp> ...)
#
# Builds one cheatah STANDARD-LIBRARY MODULE as BOTH a static (.a) and a
# versioned shared (.so) library from a single position-independent OBJECT lib —
# the same dual-artifact pattern as cheatah::linalg.
#
# These modules are the targets a cheatah program pulls in via `import <name>`.
# They are deliberately NOT linked into anything by default: only a cheatah
# executable that imports a module links its library (static for production
# builds, shared for fast iteration / hot-reload). The module's directory is its
# public include dir, so `import io` maps to <io.hpp> + libcheatah_io.
#
# add_cheatah_library(<name> SOURCES <a.cpp> ... [DEPENDS <other-module> ...])
#
# DEPENDS names the OTHER cheatah modules this one calls into, and wires all three artifacts:
# the object library gets their include directories, and the shared and static libraries actually
# LINK against them.
#
# That last part is not optional on every platform, which is how it came to exist. An ELF shared
# object may be left with undefined symbols, resolved later by whoever loads it — so on Linux a
# module whose .so referenced `cheatah::tls::last_error` linked happily without tls. A Mach-O dylib
# may not: macOS requires every symbol resolved at link time, so the same libraries failed with
# "Undefined symbols" the moment CI first built them on Apple Silicon. Declaring the dependency
# once here fixes it for every module and keeps the two platforms building the same graph.
#
# Produces targets:
#   cheatah::<name>_static   ->  libcheatah_<name>.a
#   cheatah::<name>_shared   ->  libcheatah_<name>.so  (.dylib on macOS)
include_guard(GLOBAL)  # may be included from both the root and stdlib/

function(add_cheatah_library NAME)
    cmake_parse_arguments(PL "" "" "SOURCES;DEPENDS" ${ARGN})

    set(obj cheatah_${NAME}_obj)
    add_library(${obj} OBJECT ${PL_SOURCES})
    set_target_properties(${obj} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(${obj} PUBLIC cxx_std_20)
    target_include_directories(${obj} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    set(shared cheatah_${NAME}_shared)
    set(static cheatah_${NAME}_static)
    add_library(${shared} SHARED $<TARGET_OBJECTS:${obj}>)
    add_library(${static} STATIC $<TARGET_OBJECTS:${obj}>)
    add_library(cheatah::${NAME}_shared ALIAS ${shared})
    add_library(cheatah::${NAME}_static ALIAS ${static})

    foreach(_lib ${shared} ${static})
        target_include_directories(${_lib} PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<INSTALL_INTERFACE:include>)
        set_target_properties(${_lib} PROPERTIES
            OUTPUT_NAME cheatah_${NAME}
            CLEAN_DIRECT_OUTPUT 1)
    endforeach()

    set_target_properties(${shared} PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})

    # Wire the declared dependencies to all three artifacts: headers for the object library, real
    # links for the two real libraries. Shared links shared and static links static, so neither
    # flavour drags in the other.
    foreach(_dep ${PL_DEPENDS})
        target_link_libraries(${obj} PRIVATE cheatah_${_dep}_obj)
        target_link_libraries(${shared} PRIVATE cheatah::${_dep}_shared)
        target_link_libraries(${static} PRIVATE cheatah::${_dep}_static)
    endforeach()

    # Register both artifacts so the `cheatah_stdlib` aggregate target can build
    # every module's static + shared library in one go.
    set_property(GLOBAL APPEND PROPERTY CHEATAH_STDLIB_TARGETS ${static} ${shared})
endfunction()
