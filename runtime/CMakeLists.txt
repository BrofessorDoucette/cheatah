# ---- cheatah runtime: host for compiled cheatah programs ----
#
# cheatah-runtime — the executable that dlopens a purrc-built module and runs it.
# Fully headless: no graphics, no external dependencies. A compiled program is
# self-contained (it statically links the stdlib it imported), so the host only
# validates the module path, dlopens it, and calls purr_main().
add_executable(cheatah-runtime main.cpp)

# cheatah::version (defined in compiler/, added before this dir) gives the runtime
# the SAME version string purrc reports, with no duplication.
target_link_libraries(cheatah-runtime PRIVATE cheatah::version ${CMAKE_DL_LIBS})
