# cmake/CheatahProgram.cmake
#
# The PUBLIC, consumable helper a downstream cheatah project uses to build a
# runnable cheatah program from a `.purr` source. A project pulls the cheatah
# toolchain in via CPM (`CPMAddPackage(NAME cheatah …)`) and then:
#
#   include(${cheatah_SOURCE_DIR}/cmake/CheatahProgram.cmake)
#   cheatah_add_program(myapp SOURCES src/main.purr)
#
# `biome` (cheatah's package manager) generates exactly that CMakeLists.txt from a
# project's cheatah.toml, so "everything is handled by CMake": configuring the
# project builds purrc (as a CPM subproject), runs it to compile the program into a
# loadable MODULE, and builds a small launcher that runs that module via the
# cheatah runtime.
#
# A cheatah program is ALWAYS a purrc-built module (.so/.dylib/.dll) executed by the
# `cheatah` runtime — purrc never emits a standalone executable. cheatah_add_program
# additionally builds a native launcher named <name> so the program is invoked as
# `myapp <args>` (the launcher re-execs `cheatah myapp.<ext> <args>`); the launcher
# carries no program logic, so compiled cheatah code still only ever runs under the
# runtime. The same helper builds `biome` itself.
include_guard(GLOBAL)

# The launcher source ships next to this helper; capture its absolute path at the
# FIRST include (works both in-tree and when included from ${cheatah_SOURCE_DIR}).
# Stored globally (CACHE INTERNAL) because include_guard(GLOBAL) means a second
# include in a consumer scope is skipped, so a plain variable would be unset there.
set(_CHEATAH_LAUNCHER_SRC "${CMAKE_CURRENT_LIST_DIR}/cheatah_launcher.cpp"
    CACHE INTERNAL "cheatah program launcher source")

# cheatah_add_program(<name> SOURCES <prog.purr> [EXTENSIONS <ext> ...] [CXXFLAGS <flag> ...])
function(cheatah_add_program NAME)
    cmake_parse_arguments(CAP "" "" "SOURCES;EXTENSIONS;IMPORT_ROOTS;CXXFLAGS" ${ARGN})
    if(NOT CAP_SOURCES)
        message(FATAL_ERROR "cheatah_add_program(${NAME}): SOURCES is required")
    endif()
    list(LENGTH CAP_SOURCES _n_src)
    if(NOT _n_src EQUAL 1)
        message(FATAL_ERROR "cheatah_add_program(${NAME}): exactly one .purr source is supported today")
    endif()
    if(NOT TARGET purrc OR NOT TARGET cheatah)
        message(FATAL_ERROR "cheatah_add_program(${NAME}): the cheatah toolchain (purrc + runtime) is not "
                            "available; add it first with CPMAddPackage(NAME cheatah …)")
    endif()

    # Land the program next to the other cheatah tools when a runtime output dir is
    # configured (this repo's own build), otherwise in the build root (a downstream
    # project: biome's `run` expects build/<name>).
    if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(_outdir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    else()
        set(_outdir "${CMAKE_BINARY_DIR}")
    endif()

    # The platform module extension (mirrors cmake/Portability.cmake; computed here so
    # the helper has no dependency on that file being included in the consumer scope).
    if(WIN32)
        set(_ext ".dll")
    elseif(APPLE)
        set(_ext ".dylib")
    else()
        set(_ext ".so")
    endif()
    set(_module "${_outdir}/${NAME}${_ext}")

    get_filename_component(_src "${CAP_SOURCES}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

    # Optional standard-library extensions: each is a CPM-fetched repo that emits a cheatah
    # module (via cheatah_add_module) into its own source tree. purrc resolves an imported
    # module by searching CHEATAH_MODULE_PATH (then the baked toolchain root), verifying the
    # module's signed header and linking its archive — so wiring an extension in is just adding
    # the fetched repo's directory to that search path for the purrc invocation below.
    set(_cheatah_modpath "")
    foreach(_ext ${CAP_EXTENSIONS})
        if(DEFINED ${_ext}_SOURCE_DIR)
            if(_cheatah_modpath)
                set(_cheatah_modpath "${_cheatah_modpath}:${${_ext}_SOURCE_DIR}")
            else()
                set(_cheatah_modpath "${${_ext}_SOURCE_DIR}")
            endif()
        else()
            message(WARNING "cheatah_add_program(${NAME}): extension '${_ext}' was not fetched "
                            "(no ${_ext}_SOURCE_DIR) — `import` of its modules will not resolve")
        endif()
    endforeach()
    # Prepend the env-setter only when there ARE extensions, so an extension-less program (e.g.
    # biome itself) runs purrc exactly as before.
    set(_purrc_env "")
    if(_cheatah_modpath)
        set(_purrc_env ${CMAKE_COMMAND} -E env "CHEATAH_MODULE_PATH=${_cheatah_modpath}")
    endif()

    # IMPORT_ROOTS: directories purrc should resolve `import`s from — the project's local/path
    # dependencies (biome emits one per `cheatah.toml [dependencies]` entry). Each becomes a
    # `--import-root <dir>` so `import pkg.mod` finds <dir>/pkg/mod.hpp (or a sibling header)
    # with no signing/archive convention; any compiled library a dep ships is linked by the
    # build, not the compiler.
    set(_import_root_flags "")
    foreach(_r ${CAP_IMPORT_ROOTS})
        list(APPEND _import_root_flags --import-root "${_r}")
    endforeach()

    # CXXFLAGS: extra C++ COMPILE flags forwarded to purrc's backend (`--cxxflag <flag>`). An
    # extension that needs specific compile options (e.g. cheatah-gpu's Metal build wants -fblocks,
    # -DCHEATAH_GPU_BACKEND_METAL, an -I<sdk>) declares them; biome passes them through here. (purrc
    # also honours the CHEATAH_CXXFLAGS_EXTRA environment variable for a whole build tree.)
    set(_cxxflag_flags "")
    foreach(_f ${CAP_CXXFLAGS})
        list(APPEND _cxxflag_flags --cxxflag "${_f}")
    endforeach()

    # 1) Compile the program into a loadable module with purrc (NEVER standalone).
    add_custom_command(
        OUTPUT "${_module}"
        COMMAND ${_purrc_env} $<TARGET_FILE:purrc> ${_import_root_flags} ${_cxxflag_flags} "${_src}" -o "${_module}"
        DEPENDS purrc cheatah_stdlib "${_src}"
        COMMENT "purrc ${CAP_SOURCES} -> ${NAME}${_ext}"
        VERBATIM)
    add_custom_target(${NAME}_module ALL DEPENDS "${_module}")

    # 2) The launcher executable: runs the module via the cheatah runtime, so the
    #    program is invoked as `${NAME} <args>` and its code only runs under `cheatah`.
    add_executable(${NAME} "${_CHEATAH_LAUNCHER_SRC}")
    target_compile_features(${NAME} PRIVATE cxx_std_17)
    target_compile_definitions(${NAME} PRIVATE
        CHEATAH_PROGRAM_NAME="${NAME}"
        CHEATAH_MODULE_EXT="${_ext}"
        CHEATAH_RUNTIME_FALLBACK="$<TARGET_FILE:cheatah>"
        CHEATAH_MODULE_FALLBACK="${_module}")
    set_target_properties(${NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${_outdir}"
        OUTPUT_NAME "${NAME}")
    add_dependencies(${NAME} ${NAME}_module cheatah)
endfunction()
