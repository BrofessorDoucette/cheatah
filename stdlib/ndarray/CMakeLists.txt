# cheatah ndarray module — numpy-flavored N-d arrays with broadcasting.
# Its own static + shared library; linked only on `import ndarray`. Compiled at
# -O3 -march=native so the contiguous element-wise loops AUTO-VECTORIZE (SIMD).
add_cheatah_library(ndarray SOURCES ndarray.cpp ufunc_simd.cpp)
target_compile_options(cheatah_ndarray_obj PRIVATE -O3 -march=native -funroll-loops)
# The element-wise math kernels (ufunc_simd.cpp) vectorize their transcendentals
# through glibc's libmvec. `-fveclib=libmvec` maps the libm calls to the vector ABI
# (_ZGVdN4v_exp, …) and `-fno-math-errno` lets the loop vectorize at all — together,
# WITHOUT -ffast-math, so results stay strictly IEEE (no reassociation, no finite-only
# assumption); libmvec is itself correctly-rounded-ish (≤4 ulp) and IEEE on inf/NaN.
set_source_files_properties(ufunc_simd.cpp PROPERTIES
    COMPILE_OPTIONS "-fveclib=libmvec;-fno-math-errno")
