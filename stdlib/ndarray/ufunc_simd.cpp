// Vectorized double-precision element-wise math kernels for ndarray's ufuncs.
//
// This translation unit is compiled IN ISOLATION with -ffast-math (see CMakeLists.txt)
// so the transcendentals (exp/log/sin/cos/tan) auto-vectorize through glibc's libmvec.
// The default -O3 -march=native build cannot vectorize them: a std::exp call carries
// errno and the no-finite-values assumption that the vector ABI needs, so the loop
// stays scalar (~4-8x slower than NumPy's SVML-backed ufuncs).
//
// Why this is safe to isolate here, and ONLY here:
//   * The kernels are pure element-wise maps — there are NO reductions, so the
//     reassociation -ffast-math permits has nothing to reorder; results are
//     bit-identical to the strict library for finite inputs.
//   * libmvec is IEEE-correct on special values (exp(inf)=inf, sin(NaN)=NaN,
//     log(-1)=NaN, …), verified to match the strict library — the -ffinite-math-only
//     assumption only lets the compiler optimize *surrounding* code, of which a bare
//     `for i: out[i] = f(in[i])` loop has none.
//   * The flag is scoped to this file, so every other part of cheatah keeps strict
//     IEEE arithmetic (notably the linalg reductions stay exactly rounded).
// Only the contiguous double path routes here; float / strided / non-double arrays
// fall back to the generic scalar map in ndarray.hpp.
#include <cmath>
#include <cstddef>

namespace cheatah::ndarray::detail {

#define CHEATAH_UFUNC_KERNEL(NAME, FN)                            \
    void NAME(const double* in, double* out, std::size_t n) {     \
        for (std::size_t i = 0; i < n; ++i) out[i] = FN(in[i]);   \
    }

CHEATAH_UFUNC_KERNEL(simd_sqrt_f64, std::sqrt)
CHEATAH_UFUNC_KERNEL(simd_cbrt_f64, std::cbrt)
CHEATAH_UFUNC_KERNEL(simd_exp_f64, std::exp)
CHEATAH_UFUNC_KERNEL(simd_log_f64, std::log)
CHEATAH_UFUNC_KERNEL(simd_sin_f64, std::sin)
CHEATAH_UFUNC_KERNEL(simd_cos_f64, std::cos)
CHEATAH_UFUNC_KERNEL(simd_tan_f64, std::tan)

#undef CHEATAH_UFUNC_KERNEL

}  // namespace cheatah::ndarray::detail
