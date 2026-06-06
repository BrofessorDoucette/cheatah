# add_purrscript_library(<name> SOURCES <a.cpp> ...)
#
# Builds one purrscript STANDARD-LIBRARY MODULE as BOTH a static (.a) and a
# versioned shared (.so) library from a single position-independent OBJECT lib —
# the same dual-artifact pattern as cheatah::linalg.
#
# These modules are the targets a purrscript program pulls in via `import <name>`.
# They are deliberately NOT linked into anything by default: only a purrscript
# executable that imports a module links its library (static for production
# builds, shared for fast iteration / hot-reload). The module's directory is its
# public include dir, so `import io` maps to <io.hpp> + libcheatah_purrscript_io.
#
# Produces targets:
#   cheatah::purrscript_<name>_static   ->  libcheatah_purrscript_<name>.a
#   cheatah::purrscript_<name>_shared   ->  libcheatah_purrscript_<name>.so
include_guard(GLOBAL)  # may be included from both the root and purrscript/

function(add_purrscript_library NAME)
    cmake_parse_arguments(PL "" "" "SOURCES" ${ARGN})

    set(obj cheatah_purrscript_${NAME}_obj)
    add_library(${obj} OBJECT ${PL_SOURCES})
    set_target_properties(${obj} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(${obj} PUBLIC cxx_std_20)
    target_include_directories(${obj} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    set(shared cheatah_purrscript_${NAME}_shared)
    set(static cheatah_purrscript_${NAME}_static)
    add_library(${shared} SHARED $<TARGET_OBJECTS:${obj}>)
    add_library(${static} STATIC $<TARGET_OBJECTS:${obj}>)
    add_library(cheatah::purrscript_${NAME}_shared ALIAS ${shared})
    add_library(cheatah::purrscript_${NAME}_static ALIAS ${static})

    foreach(_lib ${shared} ${static})
        target_include_directories(${_lib} PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<INSTALL_INTERFACE:include>)
        set_target_properties(${_lib} PROPERTIES
            OUTPUT_NAME cheatah_purrscript_${NAME}
            CLEAN_DIRECT_OUTPUT 1)
    endforeach()

    set_target_properties(${shared} PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
endfunction()
