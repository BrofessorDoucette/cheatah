// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// cheatah::fixarray::Fixed vs GLM — the COMPLETE overlap of the two APIs, at every size and both
// precisions. glm_compare_bench.cpp measures the dynamic NDArray on GLM's home turf (and loses on
// purpose: a heap allocation to move sixteen doubles). This file measures the fixed-extent types,
// which exist precisely so that comparison is winnable.
//
// Every operation appears as a `_fixed` / `_glm` pair with identical inputs and identical work, so a
// regression anywhere is a single line to read. Both sides compile with the same flags in the same
// TU: no -march=native, so neither gets an ISA the other lacks, and any difference is code shape.
//
// Inputs are re-read through DoNotOptimize each iteration, so nothing is hoisted or folded away.
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>  // glm::inverseTranspose
#include <glm/gtc/type_ptr.hpp>        // glm::value_ptr — the flat, column-major buffer
#include <glm/gtx/norm.hpp>            // glm::length2, glm::distance2

#include <benchmark/benchmark.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "fixarray.hpp"

namespace fa = cheatah::fixarray;

namespace {

// ---- deterministic, well-conditioned inputs, identical on both sides ---------------------------

/// A cheatah Fixed vector/matrix filled the SAME way as gmfill below, so the two sides of every
/// benchmark operate on identical inputs (the parity check enforces this): element (r, c) is
/// `base + r*cols + c`, with a diagonal boost for matrices so `inverse` is meaningful. Indexing by
/// (r, c) — not by flat position — is what keeps it in step with GLM, since the two libraries store
/// a matrix in opposite orders.
template <class L>
L lfill(double base) {
    L v;
    using T = typename L::value_type;
    if constexpr (L::rank == 1) {
        for (std::size_t i = 0; i < L::size; ++i) { v[i] = static_cast<T>(base + double(i)); }
    } else {
        for (std::size_t r = 0; r < L::rows; ++r) {
            for (std::size_t c = 0; c < L::cols; ++c) {
                T x = static_cast<T>(base + double(r * L::cols + c));
                if (r == c) { x += static_cast<T>(10 * L::rows); }
                v(r, c) = x;
            }
        }
    }
    return v;
}

/// The same entries in a GLM vector.
template <class G>
G gvfill(double base) {
    G v(0);
    for (int i = 0; i < G::length(); ++i) { v[i] = typename G::value_type(base + double(i)); }
    return v;
}

/// The same entries in a GLM matrix. GLM is column-major, so `m[c][r]` mirrors our `(r, c)`.
template <class G>
G gmfill(double base) {
    G m(1);
    const int n = G::length();
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            auto v = typename G::value_type(base + double(r * n + c));
            if (r == c) { v += typename G::value_type(10 * n); }
            m[c][r] = v;
        }
    }
    return m;
}

// ---- output parity: a benchmark that times the wrong answer is worthless ------------------------
// Before anything is timed, prove Fixed and GLM compute the SAME result for every operation this
// file benchmarks. Both store column-major, so a Fixed's data() and GLM's value_ptr line up flat;
// scalar results (dot, length, det, …) compare directly. On any mismatch the binary aborts with the
// offending op named, so a benchmark can never quietly compare against a different computation.

/// True iff the flat buffers agree to a float tolerance (arrays: vectors and matrices alike).
template <class L, class G>
bool same_buffer(const L& l, const G& g) {
    const auto* gp = glm::value_ptr(g);
    for (std::size_t i = 0; i < L::size; ++i) {
        if (std::fabs(static_cast<double>(l.data()[i]) - static_cast<double>(gp[i])) > 1e-4) {
            return false;
        }
    }
    return true;
}

/// True iff two scalars agree to a float tolerance.
inline bool same_scalar(double a, double b) { return std::fabs(a - b) < 1e-4; }

void abort_if(bool ok, const char* op) {
    if (!ok) {
        static_cast<void>(std::fprintf(stderr, "\nfixed_glm_bench: OUTPUT MISMATCH in '%s' — Fixed and GLM disagree.\n",
                     op));
        std::abort();
    }
}

/// Check one vector op family (L is a Fixed vector, G the matching GLM vector) at the same inputs the
/// benchmarks use, so the verification and the measurement are of the same computation.
template <class L, class G>
void verify_vec() {
    const L la_a = lfill<L>(1.0), la_b = lfill<L>(2.0);
    const G g_a = gvfill<G>(1.0), g_b = gvfill<G>(2.0);
    using T = typename L::value_type;
    using GT = typename G::value_type;
    abort_if(same_buffer(la_a + la_b, g_a + g_b), "vec +");
    abort_if(same_buffer(la_a - la_b, g_a - g_b), "vec -");
    abort_if(same_buffer(-la_a, -g_a), "vec neg");
    abort_if(same_buffer(la_a * T(2), g_a * GT(2)), "vec * scalar");
    abort_if(same_buffer(la_a / T(2), g_a / GT(2)), "vec / scalar");
    abort_if(same_scalar(fa::dot(la_a, la_b), glm::dot(g_a, g_b)), "dot");
    abort_if(same_scalar(fa::norm(la_a), glm::length(g_a)), "length");
    abort_if(same_scalar(fa::squared_norm(la_a), glm::length2(g_a)), "length2");
    abort_if(same_buffer(fa::normalize(la_a), glm::normalize(g_a)), "normalize");
    abort_if(same_scalar(fa::distance(la_a, la_b), glm::distance(g_a, g_b)), "distance");
    abort_if(same_scalar(fa::distance_squared(la_a, la_b), glm::distance2(g_a, g_b)), "distance2");
    abort_if(same_buffer(fa::reflect(la_a, fa::normalize(la_b)), glm::reflect(g_a, glm::normalize(g_b))),
             "reflect");
    abort_if(same_buffer(fa::abs(la_a), glm::abs(g_a)), "abs");
    abort_if(same_buffer(fa::sign(la_a), glm::sign(g_a)), "sign");
    abort_if(same_buffer(fa::min(la_a, la_b), glm::min(g_a, g_b)), "min");
    abort_if(same_buffer(fa::max(la_a, la_b), glm::max(g_a, g_b)), "max");
    abort_if(same_buffer(fa::clamp(la_a, T(0), T(1)), glm::clamp(g_a, GT(0), GT(1))), "clamp");
    abort_if(same_buffer(fa::mix(la_a, la_b, T(0.5)), glm::mix(g_a, g_b, GT(0.5))), "mix");
    abort_if(same_buffer(fa::step(T(1), la_a), glm::step(GT(1), g_a)), "step");
    abort_if(same_buffer(fa::smoothstep(T(0), T(2), la_a), glm::smoothstep(GT(0), GT(2), g_a)),
             "smoothstep");
}

/// Check one matrix op family (M a Fixed square matrix, G the matching GLM matrix; V/GV the vectors).
template <class M, class G, class V, class GV>
void verify_mat() {
    const M la_a = lfill<M>(1.0), la_b = lfill<M>(2.0);
    const G g_a = gmfill<G>(1.0), g_b = gmfill<G>(2.0);
    const V la_v = lfill<V>(1.0);
    const GV g_v = gvfill<GV>(1.0);
    using T = typename M::value_type;
    using GT = typename G::value_type;
    abort_if(same_buffer(la_a + la_b, g_a + g_b), "mat +");
    abort_if(same_buffer(la_a * T(2), g_a * GT(2)), "mat * scalar");
    abort_if(same_buffer(fa::matmul(la_a, la_b), g_a * g_b), "matmul");
    abort_if(same_buffer(la_a * la_v, g_a * g_v), "mat * vec");
    abort_if(same_buffer(fa::transpose(la_a), glm::transpose(g_a)), "transpose");
    abort_if(same_scalar(fa::determinant(la_a), glm::determinant(g_a)), "determinant");
    abort_if(same_buffer(fa::inverse(la_a), glm::inverse(g_a)), "inverse");
    abort_if(same_buffer(fa::matrix_comp_mult(la_a, la_b), glm::matrixCompMult(g_a, g_b)),
             "matrixCompMult");
    abort_if(same_buffer(fa::outer_product(la_v, la_v), glm::outerProduct(g_v, g_v)), "outerProduct");
    abort_if(same_buffer(fa::inverse_transpose(la_a), glm::inverseTranspose(g_a)), "inverseTranspose");
    abort_if(same_buffer(M::identity(), G(1)), "identity");
}

/// Runs at static init, before any benchmark: the whole suite verifies against GLM, or aborts.
const bool kOutputsVerified = [] {
    verify_vec<fa::vec3f, glm::vec3>();
    verify_vec<fa::vec4f, glm::vec4>();
    verify_vec<fa::vec3d, glm::dvec3>();
    verify_vec<fa::vec4d, glm::dvec4>();
    verify_mat<fa::mat3f, glm::mat3, fa::vec3f, glm::vec3>();
    verify_mat<fa::mat4f, glm::mat4, fa::vec4f, glm::vec4>();
    verify_mat<fa::mat3d, glm::dmat3, fa::vec3d, glm::dvec3>();
    verify_mat<fa::mat4d, glm::dmat4, fa::vec4d, glm::dvec4>();
    // cross is 3-D only.
    abort_if(same_buffer(fa::cross(lfill<fa::vec3f>(1.0), lfill<fa::vec3f>(2.0)),
                         glm::cross(gvfill<glm::vec3>(1.0), gvfill<glm::vec3>(2.0))),
             "cross");
    static_cast<void>(std::fprintf(stderr, "fixed_glm_bench: outputs verified against GLM on all benchmarked ops.\n"));
    return true;
}();

// ---- vector operations --------------------------------------------------------------------------

#define VEC_BENCH(NAME, FIXED_EXPR, GLM_EXPR)                                    \
    template <class L>                                                           \
    void bm_##NAME##_fixed(benchmark::State& state) {                            \
        L a = lfill<L>(1.0), b = lfill<L>(2.0);                                  \
        for (auto _ : state) {                                                   \
            benchmark::DoNotOptimize(a);                                         \
            benchmark::DoNotOptimize(b);                                         \
            auto c = (FIXED_EXPR);                                               \
            benchmark::DoNotOptimize(&c);                                        \
        }                                                                        \
    }                                                                            \
    template <class G>                                                           \
    void bm_##NAME##_glm(benchmark::State& state) {                              \
        G a = gvfill<G>(1.0), b = gvfill<G>(2.0);                                \
        for (auto _ : state) {                                                   \
            benchmark::DoNotOptimize(a);                                         \
            benchmark::DoNotOptimize(b);                                         \
            auto c = (GLM_EXPR);                                                 \
            benchmark::DoNotOptimize(&c);                                        \
        }                                                                        \
    }

VEC_BENCH(vadd, a + b, a + b)
VEC_BENCH(vsub, a - b, a - b)
VEC_BENCH(vneg, -a, -a)
VEC_BENCH(vmuls, a * typename L::value_type(2), a* typename G::value_type(2))
VEC_BENCH(vdivs, a / typename L::value_type(2), a / typename G::value_type(2))
VEC_BENCH(vdot, fa::dot(a, b), glm::dot(a, b))
VEC_BENCH(vlen, fa::norm(a), glm::length(a))
VEC_BENCH(vlen2, fa::squared_norm(a), glm::length2(a))
VEC_BENCH(vnorm, fa::normalize(a), glm::normalize(a))
VEC_BENCH(vcross, fa::cross(a, b), glm::cross(a, b))

#define PAIR_VEC_COMMON(L, G, TAG)                                    \
    BENCHMARK(bm_vadd_fixed<L>)->Name("BM_add_" TAG "_fixed");        \
    BENCHMARK(bm_vadd_glm<G>)->Name("BM_add_" TAG "_glm");            \
    BENCHMARK(bm_vsub_fixed<L>)->Name("BM_sub_" TAG "_fixed");        \
    BENCHMARK(bm_vsub_glm<G>)->Name("BM_sub_" TAG "_glm");            \
    BENCHMARK(bm_vneg_fixed<L>)->Name("BM_neg_" TAG "_fixed");        \
    BENCHMARK(bm_vneg_glm<G>)->Name("BM_neg_" TAG "_glm");            \
    BENCHMARK(bm_vmuls_fixed<L>)->Name("BM_muls_" TAG "_fixed");      \
    BENCHMARK(bm_vmuls_glm<G>)->Name("BM_muls_" TAG "_glm");          \
    BENCHMARK(bm_vdivs_fixed<L>)->Name("BM_divs_" TAG "_fixed");      \
    BENCHMARK(bm_vdivs_glm<G>)->Name("BM_divs_" TAG "_glm");          \
    BENCHMARK(bm_vdot_fixed<L>)->Name("BM_dot_" TAG "_fixed");        \
    BENCHMARK(bm_vdot_glm<G>)->Name("BM_dot_" TAG "_glm");            \
    BENCHMARK(bm_vlen_fixed<L>)->Name("BM_len_" TAG "_fixed");        \
    BENCHMARK(bm_vlen_glm<G>)->Name("BM_len_" TAG "_glm");            \
    BENCHMARK(bm_vlen2_fixed<L>)->Name("BM_len2_" TAG "_fixed");      \
    BENCHMARK(bm_vlen2_glm<G>)->Name("BM_len2_" TAG "_glm");          \
    BENCHMARK(bm_vnorm_fixed<L>)->Name("BM_normalize_" TAG "_fixed"); \
    BENCHMARK(bm_vnorm_glm<G>)->Name("BM_normalize_" TAG "_glm");

PAIR_VEC_COMMON(fa::vec2f, glm::vec2, "vec2f")
PAIR_VEC_COMMON(fa::vec3f, glm::vec3, "vec3f")
PAIR_VEC_COMMON(fa::vec4f, glm::vec4, "vec4f")
PAIR_VEC_COMMON(fa::vec2d, glm::dvec2, "vec2d")
PAIR_VEC_COMMON(fa::vec3d, glm::dvec3, "vec3d")
PAIR_VEC_COMMON(fa::vec4d, glm::dvec4, "vec4d")

BENCHMARK(bm_vcross_fixed<fa::vec3f>)->Name("BM_cross_vec3f_fixed");
BENCHMARK(bm_vcross_glm<glm::vec3>)->Name("BM_cross_vec3f_glm");
BENCHMARK(bm_vcross_fixed<fa::vec3d>)->Name("BM_cross_vec3d_fixed");
BENCHMARK(bm_vcross_glm<glm::dvec3>)->Name("BM_cross_vec3d_glm");

// ---- matrix operations --------------------------------------------------------------------------

#define MAT_BENCH(NAME, FIXED_EXPR, GLM_EXPR)                                    \
    template <class L>                                                           \
    void bm_##NAME##_fixed(benchmark::State& state) {                            \
        L a = lfill<L>(1.0), b = lfill<L>(2.0);                                  \
        for (auto _ : state) {                                                   \
            benchmark::DoNotOptimize(a);                                         \
            benchmark::DoNotOptimize(b);                                         \
            auto c = (FIXED_EXPR);                                               \
            benchmark::DoNotOptimize(&c);                                        \
        }                                                                        \
    }                                                                            \
    template <class G>                                                           \
    void bm_##NAME##_glm(benchmark::State& state) {                              \
        G a = gmfill<G>(1.0), b = gmfill<G>(2.0);                                \
        for (auto _ : state) {                                                   \
            benchmark::DoNotOptimize(a);                                         \
            benchmark::DoNotOptimize(b);                                         \
            auto c = (GLM_EXPR);                                                 \
            benchmark::DoNotOptimize(&c);                                        \
        }                                                                        \
    }

MAT_BENCH(madd, a + b, a + b)
MAT_BENCH(mmuls, a * typename L::value_type(2), a* typename G::value_type(2))
MAT_BENCH(mmul, fa::matmul(a, b), a* b)
MAT_BENCH(mtrans, fa::transpose(a), glm::transpose(a))
MAT_BENCH(mdet, fa::determinant(a), glm::determinant(a))
MAT_BENCH(minv, fa::inverse(a), glm::inverse(a))

/// Matrix * vector, the transform a renderer applies per vertex.
template <class L, class V>
void bm_matvec_fixed(benchmark::State& state) {
    L m = lfill<L>(1.0);
    V v = lfill<V>(1.0);
    for (auto _ : state) {
        benchmark::DoNotOptimize(m);
        benchmark::DoNotOptimize(v);
        auto c = m * v;
        benchmark::DoNotOptimize(&c);
    }
}
template <class G, class GV>
void bm_matvec_glm(benchmark::State& state) {
    G m = gmfill<G>(1.0);
    GV v = gvfill<GV>(1.0);
    for (auto _ : state) {
        benchmark::DoNotOptimize(m);
        benchmark::DoNotOptimize(v);
        auto c = m * v;
        benchmark::DoNotOptimize(&c);
    }
}

/// Building the identity — a renderer does this every time it resets a transform.
template <class L>
void bm_identity_fixed(benchmark::State& state) {
    for (auto _ : state) {
        auto c = L::identity();
        benchmark::DoNotOptimize(&c);
    }
}
template <class G>
void bm_identity_glm(benchmark::State& state) {
    for (auto _ : state) {
        G c(1);
        benchmark::DoNotOptimize(&c);
    }
}

#define PAIR_MAT(L, G, V, GV, TAG)                                          \
    BENCHMARK(bm_madd_fixed<L>)->Name("BM_add_" TAG "_fixed");              \
    BENCHMARK(bm_madd_glm<G>)->Name("BM_add_" TAG "_glm");                  \
    BENCHMARK(bm_mmuls_fixed<L>)->Name("BM_muls_" TAG "_fixed");            \
    BENCHMARK(bm_mmuls_glm<G>)->Name("BM_muls_" TAG "_glm");                \
    BENCHMARK(bm_mmul_fixed<L>)->Name("BM_matmul_" TAG "_fixed");           \
    BENCHMARK(bm_mmul_glm<G>)->Name("BM_matmul_" TAG "_glm");               \
    BENCHMARK(bm_mtrans_fixed<L>)->Name("BM_transpose_" TAG "_fixed");      \
    BENCHMARK(bm_mtrans_glm<G>)->Name("BM_transpose_" TAG "_glm");          \
    BENCHMARK(bm_mdet_fixed<L>)->Name("BM_det_" TAG "_fixed");              \
    BENCHMARK(bm_mdet_glm<G>)->Name("BM_det_" TAG "_glm");                  \
    BENCHMARK(bm_minv_fixed<L>)->Name("BM_inverse_" TAG "_fixed");          \
    BENCHMARK(bm_minv_glm<G>)->Name("BM_inverse_" TAG "_glm");              \
    BENCHMARK((bm_matvec_fixed<L, V>))->Name("BM_matvec_" TAG "_fixed");    \
    BENCHMARK((bm_matvec_glm<G, GV>))->Name("BM_matvec_" TAG "_glm");       \
    BENCHMARK(bm_identity_fixed<L>)->Name("BM_identity_" TAG "_fixed");     \
    BENCHMARK(bm_identity_glm<G>)->Name("BM_identity_" TAG "_glm");

PAIR_MAT(fa::mat2f, glm::mat2, fa::vec2f, glm::vec2, "mat2f")
PAIR_MAT(fa::mat3f, glm::mat3, fa::vec3f, glm::vec3, "mat3f")
PAIR_MAT(fa::mat4f, glm::mat4, fa::vec4f, glm::vec4, "mat4f")
PAIR_MAT(fa::mat2d, glm::dmat2, fa::vec2d, glm::dvec2, "mat2d")
PAIR_MAT(fa::mat3d, glm::dmat3, fa::vec3d, glm::dvec3, "mat3d")
PAIR_MAT(fa::mat4d, glm::dmat4, fa::vec4d, glm::dvec4, "mat4d")

// ---- the GLSL/GLM "common" and geometric surface ------------------------------------------------
// The rest of the overlap: distance/reflect/refract on vectors, and the component-wise builtins.
// Same DoNotOptimize discipline; VEC_BENCH already fixes the two vector operands a=lfill(1), b=lfill(2)
// (for GLM, gvfill), so these reuse it. A few need three operands or a scalar, written out here.

VEC_BENCH(distance, fa::distance(a, b), glm::distance(a, b))
VEC_BENCH(distance2, fa::distance_squared(a, b), glm::distance2(a, b))
VEC_BENCH(reflect, fa::reflect(a, fa::normalize(b)), glm::reflect(a, glm::normalize(b)))
VEC_BENCH(vabs, fa::abs(a), glm::abs(a))
VEC_BENCH(vsign, fa::sign(a), glm::sign(a))
VEC_BENCH(vmin, fa::min(a, b), glm::min(a, b))
VEC_BENCH(vmax, fa::max(a, b), glm::max(a, b))
VEC_BENCH(vclamp, fa::clamp(a, typename L::value_type(0), typename L::value_type(1)),
          glm::clamp(a, typename G::value_type(0), typename G::value_type(1)))
VEC_BENCH(vmix, fa::mix(a, b, typename L::value_type(0.5)),
          glm::mix(a, b, typename G::value_type(0.5)))
VEC_BENCH(vstep, fa::step(typename L::value_type(1), a), glm::step(typename G::value_type(1), a))
VEC_BENCH(vsmoothstep, fa::smoothstep(typename L::value_type(0), typename L::value_type(2), a),
          glm::smoothstep(typename G::value_type(0), typename G::value_type(2), a))

#define PAIR_COMMON(L, G, TAG)                                                    \
    BENCHMARK(bm_distance_fixed<L>)->Name("BM_distance_" TAG "_fixed");           \
    BENCHMARK(bm_distance_glm<G>)->Name("BM_distance_" TAG "_glm");               \
    BENCHMARK(bm_distance2_fixed<L>)->Name("BM_distance2_" TAG "_fixed");         \
    BENCHMARK(bm_distance2_glm<G>)->Name("BM_distance2_" TAG "_glm");             \
    BENCHMARK(bm_reflect_fixed<L>)->Name("BM_reflect_" TAG "_fixed");             \
    BENCHMARK(bm_reflect_glm<G>)->Name("BM_reflect_" TAG "_glm");                 \
    BENCHMARK(bm_vabs_fixed<L>)->Name("BM_abs_" TAG "_fixed");                    \
    BENCHMARK(bm_vabs_glm<G>)->Name("BM_abs_" TAG "_glm");                        \
    BENCHMARK(bm_vsign_fixed<L>)->Name("BM_sign_" TAG "_fixed");                  \
    BENCHMARK(bm_vsign_glm<G>)->Name("BM_sign_" TAG "_glm");                      \
    BENCHMARK(bm_vmin_fixed<L>)->Name("BM_min_" TAG "_fixed");                    \
    BENCHMARK(bm_vmin_glm<G>)->Name("BM_min_" TAG "_glm");                        \
    BENCHMARK(bm_vmax_fixed<L>)->Name("BM_max_" TAG "_fixed");                    \
    BENCHMARK(bm_vmax_glm<G>)->Name("BM_max_" TAG "_glm");                        \
    BENCHMARK(bm_vclamp_fixed<L>)->Name("BM_clamp_" TAG "_fixed");                \
    BENCHMARK(bm_vclamp_glm<G>)->Name("BM_clamp_" TAG "_glm");                    \
    BENCHMARK(bm_vmix_fixed<L>)->Name("BM_mix_" TAG "_fixed");                    \
    BENCHMARK(bm_vmix_glm<G>)->Name("BM_mix_" TAG "_glm");                        \
    BENCHMARK(bm_vstep_fixed<L>)->Name("BM_step_" TAG "_fixed");                  \
    BENCHMARK(bm_vstep_glm<G>)->Name("BM_step_" TAG "_glm");                      \
    BENCHMARK(bm_vsmoothstep_fixed<L>)->Name("BM_smoothstep_" TAG "_fixed");      \
    BENCHMARK(bm_vsmoothstep_glm<G>)->Name("BM_smoothstep_" TAG "_glm");

PAIR_COMMON(fa::vec3f, glm::vec3, "vec3f")
PAIR_COMMON(fa::vec4f, glm::vec4, "vec4f")
PAIR_COMMON(fa::vec3d, glm::dvec3, "vec3d")
PAIR_COMMON(fa::vec4d, glm::dvec4, "vec4d")

// matrixCompMult, outerProduct, inverseTranspose — matrix builtins beyond the ordinary product.
MAT_BENCH(compmult, fa::matrix_comp_mult(a, b), glm::matrixCompMult(a, b))
MAT_BENCH(invtrans, fa::inverse_transpose(a), glm::inverseTranspose(a))

template <class L, class G>
void bm_outer_fixed(benchmark::State& state) {
    L c = lfill<L>(1.0);
    L r = lfill<L>(2.0);
    for (auto _ : state) {
        benchmark::DoNotOptimize(c);
        benchmark::DoNotOptimize(r);
        auto m = fa::outer_product(c, r);
        benchmark::DoNotOptimize(&m);
    }
}
template <class GV>
void bm_outer_glm(benchmark::State& state) {
    GV c = gvfill<GV>(1.0);
    GV r = gvfill<GV>(2.0);
    for (auto _ : state) {
        benchmark::DoNotOptimize(c);
        benchmark::DoNotOptimize(r);
        auto m = glm::outerProduct(c, r);
        benchmark::DoNotOptimize(&m);
    }
}

#define PAIR_MAT_EXTRA(L, G, V, GV, TAG)                                          \
    BENCHMARK(bm_compmult_fixed<L>)->Name("BM_compmult_" TAG "_fixed");           \
    BENCHMARK(bm_compmult_glm<G>)->Name("BM_compmult_" TAG "_glm");               \
    BENCHMARK(bm_invtrans_fixed<L>)->Name("BM_invtrans_" TAG "_fixed");           \
    BENCHMARK(bm_invtrans_glm<G>)->Name("BM_invtrans_" TAG "_glm");               \
    BENCHMARK((bm_outer_fixed<V, GV>))->Name("BM_outer_" TAG "_fixed");           \
    BENCHMARK(bm_outer_glm<GV>)->Name("BM_outer_" TAG "_glm");

PAIR_MAT_EXTRA(fa::mat3f, glm::mat3, fa::vec3f, glm::vec3, "mat3f")
PAIR_MAT_EXTRA(fa::mat4f, glm::mat4, fa::vec4f, glm::vec4, "mat4f")
PAIR_MAT_EXTRA(fa::mat3d, glm::dmat3, fa::vec3d, glm::dvec3, "mat3d")
PAIR_MAT_EXTRA(fa::mat4d, glm::dmat4, fa::vec4d, glm::dvec4, "mat4d")

}  // namespace
