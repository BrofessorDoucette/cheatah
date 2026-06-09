# ---- pkg-manager: biome, the cheatah package manager ----
#
# biome is a cheatah program (biome.purr) compiled by purrc into a loadable module
# and run by the cheatah runtime — like every cheatah program. cheatah_add_program
# (the same helper downstream projects use) compiles biome.so and builds the small
# `biome` launcher next to it, so it is invoked as `biome <command> <args>` (the
# launcher re-execs the runtime on biome.so). Both land in <build>/bin alongside
# purrc and the cheatah runtime.
#
# biome itself has NO build dependencies beyond the toolchain it ships with: at
# RUNTIME it shells out to `cmake` (which uses CPM) to fetch/build the cheatah
# core and the optional stdlib extensions a project selects.
include(${PROJECT_SOURCE_DIR}/cmake/CheatahProgram.cmake)

cheatah_add_program(biome SOURCES biome.purr)
