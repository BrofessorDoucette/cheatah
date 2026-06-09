# cheatah ndarray module — numpy-flavored N-d arrays with broadcasting.
# Its own static + shared library; linked only on `import ndarray`. Compiled at
# -O3 -march=native so the contiguous element-wise loops AUTO-VECTORIZE (SIMD).
add_cheatah_library(ndarray SOURCES ndarray.cpp ufunc_simd.cpp)
target_compile_options(cheatah_ndarray_obj PRIVATE ${CHEATAH_MODULE_OPTFLAGS})
# The element-wise math kernels (ufunc_simd.cpp) vectorize their transcendentals through
# the platform's vector libm — glibc's libmvec on Linux (`-fveclib=libmvec`, symbols like
# _ZGVdN4v_exp), the Accelerate vForce on macOS (`-fveclib=Accelerate`), or scalar where
# neither exists (still correct). `-fno-math-errno` is what lets the loop vectorize at all.
# All WITHOUT -ffast-math, so results stay strictly IEEE. The exact flags come from
# cmake/Portability.cmake (CHEATAH_UFUNC_SIMD_FLAGS).
set_source_files_properties(ufunc_simd.cpp PROPERTIES
    COMPILE_OPTIONS "${CHEATAH_UFUNC_SIMD_FLAGS}")
