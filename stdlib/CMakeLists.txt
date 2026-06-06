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
# ndarray + linalg are added at the root (before this dir) so linalg sees ndarray.

# cheatah's unit tests (the compiler frontend + the stdlib modules) live here.
# The compiler tests link cheatah::compiler (defined in ../compiler, added first).
if(CHEATAH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
