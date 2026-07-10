# fixarray — header-only fixed-extent array module: Fixed<T, Dims...> (vec3f/mat4f/…), the
# compile-time-shaped, inline, column-major counterpart to ndarray's dynamic NDArray. Templates
# only — nothing is compiled — so it is an INTERFACE target: `import fixarray` (purrc) resolves the
# header, and any C++ consumer links cheatah::fixarray for the include path. It depends on ndarray
# for the element concepts (Field/Numeric/FloatingPoint) and Subscript indexing it builds on.
add_library(cheatah_fixarray INTERFACE)
add_library(cheatah::fixarray ALIAS cheatah_fixarray)
target_include_directories(cheatah_fixarray INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cheatah_fixarray INTERFACE cxx_std_20)

# ndarray carries fixarray's element concepts + Subscript; link it for the include path (it is a
# header dependency only — the templates instantiate against ndarray's inline/constexpr surface).
target_link_libraries(cheatah_fixarray INTERFACE cheatah::ndarray_static)
