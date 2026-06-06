# cheatah ndarray module — numpy-flavored N-d arrays with broadcasting.
# Its own static + shared library; linked only on `import ndarray`. Compiled at
# -O3 -march=native so the contiguous element-wise loops AUTO-VECTORIZE (SIMD).
add_cheatah_library(ndarray SOURCES ndarray.cpp)
target_compile_options(cheatah_ndarray_obj PRIVATE -O3 -march=native -funroll-loops)
