# cheatah linalg module — cheatah's SIMD-accelerated linear-algebra core +
# numpy-style routines (matmul/solve/inv/det/qr/svd/eig/…). Built as its own dual
# static+shared library; the routines operate on ndarray, so we link it.
#
# Compiled at -O3 -march=native so the contiguous hot loops AUTO-VECTORIZE (SIMD).
add_cheatah_library(linalg SOURCES simd.cpp routines.cpp)

# The routines use ndarray::NDArray (must be defined before linalg in the build).
target_link_libraries(cheatah_linalg_obj PUBLIC cheatah::ndarray_static)
foreach(_lib cheatah_linalg_static cheatah_linalg_shared)
    target_link_libraries(${_lib} PUBLIC cheatah::ndarray_static)
endforeach()

# Force SIMD optimization on the numeric hot paths regardless of build type.
target_compile_options(cheatah_linalg_obj PRIVATE -O3 -march=native -funroll-loops)
