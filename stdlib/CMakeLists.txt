# ---- cheatah standard library ----
#
# The standard-library MODULES (io, os, …) — each its own dual static+shared
# library, linked into a cheatah program ONLY when it `import`s the module. The
# language toolchain and the purrc compiler live in ../compiler.

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
add_subdirectory(socket)
add_subdirectory(sys)
# ndarray + linalg are added at the root (before this dir) so linalg sees ndarray.

# Convenience aggregate: `cmake --build … --target cheatah_stdlib` builds EVERY
# stdlib module's static + shared library at once (registered by add_cheatah_library,
# including ndarray + linalg added at the root before this point).
get_property(_cheatah_stdlib_targets GLOBAL PROPERTY CHEATAH_STDLIB_TARGETS)
add_custom_target(cheatah_stdlib DEPENDS ${_cheatah_stdlib_targets})

# cheatah's unit tests (the compiler frontend + the stdlib modules) live here.
# The compiler tests link cheatah::compiler (defined in ../compiler, added first).
if(CHEATAH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
