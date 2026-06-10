# cmake/CheatahModule.cmake
#
# Build an importable cheatah LIBRARY module from a `.purr` by running `purrc --emit-library`
# (the pure-cheatah counterpart of add_cheatah_library, which builds a C++ module). cheatah
# is templated C++ with concepts + a transpiler, so the emitted module is just generated C++:
# a signed header plus, for opaque builds, a compiled static archive.
#
#   cheatah_add_module(<name> SOURCES <name>.purr [TRANSPARENT])
#
# TRANSPARENT (used by the first-party standard library) inlines the generated C++ source
# into the committed header, so users — and the VS Code extension's Go-to-Definition — always
# see the true C++; there is no separate archive. Without it (the default an external author
# gets) the header ships only the public API and the implementation is compiled into a signed
# libcheatah_<name>.a, hiding the source.
#
# The module is built BY purrc, so this runs AFTER purrc exists (like cmake/CheatahProgram.cmake
# builds `biome`). The header is emitted next to the source and committed, so `import <name>`
# resolves and verifies it exactly like a hand-written C++ module; the QA gate guards that the
# committed header stays in sync with the `.purr`.
include_guard(GLOBAL)

function(cheatah_add_module NAME)
    cmake_parse_arguments(CAM "TRANSPARENT" "" "SOURCES" ${ARGN})
    if(NOT CAM_SOURCES)
        message(FATAL_ERROR "cheatah_add_module(${NAME}): SOURCES is required")
    endif()
    if(NOT TARGET purrc)
        message(FATAL_ERROR "cheatah_add_module(${NAME}): purrc is not available yet — add the "
                            "compiler/ subdirectory before this module")
    endif()

    get_filename_component(_src "${CAM_SOURCES}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(_srcdir "${_src}" DIRECTORY)
    set(_hdr "${_srcdir}/${NAME}.hpp")                         # committed next to the .purr
    set(_outputs "${_hdr}" "${_hdr}.sha512")
    set(_mode "")
    if(CAM_TRANSPARENT)
        set(_mode --transparent)
    else()
        list(APPEND _outputs
            "${CMAKE_CURRENT_SOURCE_DIR}/libcheatah_${NAME}.a"
            "${CMAKE_CURRENT_SOURCE_DIR}/libcheatah_${NAME}.a.sha512")
    endif()

    add_custom_command(
        OUTPUT ${_outputs}
        COMMAND $<TARGET_FILE:purrc> --emit-library ${_mode} "${_src}" -o "${_hdr}"
        DEPENDS purrc "${_src}"
        COMMENT "purrc --emit-library ${NAME} -> ${NAME}.hpp"
        VERBATIM)
    add_custom_target(cheatah_${NAME}_module ALL DEPENDS ${_outputs})
endfunction()
