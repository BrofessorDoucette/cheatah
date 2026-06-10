# ---- cheatah runtime: host for compiled cheatah programs ----
#
# cheatah — the executable that dlopens a purrc-built module and runs it.
# Fully headless: no graphics, no external dependencies. A compiled program is
# self-contained (it statically links the stdlib it imported), so the host only
# validates the module path, dlopens it, and calls purr_main().
add_executable(cheatah main.cpp integrity.cpp)
target_include_directories(cheatah PRIVATE
    ${PROJECT_SOURCE_DIR}/stdlib/hashlib ${PROJECT_SOURCE_DIR}/stdlib/ed25519)

# cheatah::version (defined in compiler/, added before this dir) gives the runtime
# the SAME version string purrc reports, with no duplication. The runtime links the
# SAME self-contained crypto the stdlib exposes (hashlib SHA-512, ed25519 verify) to
# check a module's integrity before loading it — no separate/external crypto. ed25519
# is listed before hashlib so its references resolve (static archives, left-to-right).
target_link_libraries(cheatah PRIVATE
    cheatah::version
    cheatah::ed25519_static
    cheatah::hashlib_static
    ${CMAKE_DL_LIBS})
