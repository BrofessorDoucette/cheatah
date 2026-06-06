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
# Produces targets:
#   cheatah::<name>_static   ->  libcheatah_<name>.a
#   cheatah::<name>_shared   ->  libcheatah_<name>.so
include_guard(GLOBAL)  # may be included from both the root and stdlib/

function(add_cheatah_library NAME)
    cmake_parse_arguments(PL "" "" "SOURCES" ${ARGN})

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
endfunction()
