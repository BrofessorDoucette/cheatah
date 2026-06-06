# ---- cheatah runtime: host for compiled cheatah programs ----
#
# cheatah::runtime — the host-services library a program's purr_main receives.
# cheatah-runtime  — the executable that dlopens a purrc-built module and runs it.
#
# Fully headless: no graphics, no external dependencies. The host exports its
# symbols so a dlopen'd module resolves Runtime's symbols from it.
add_library(cheatah_runtime STATIC runtime.cpp)
add_library(cheatah::runtime ALIAS cheatah_runtime)
target_include_directories(cheatah_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cheatah_runtime PUBLIC cxx_std_20)
set_target_properties(cheatah_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(cheatah-runtime main.cpp)
target_link_libraries(cheatah-runtime PRIVATE cheatah::runtime ${CMAKE_DL_LIBS})
set_target_properties(cheatah-runtime PROPERTIES ENABLE_EXPORTS ON)
