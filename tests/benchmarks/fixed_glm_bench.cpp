// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// cheatah::linalg::Fixed vs GLM — the COMPLETE overlap of the two APIs, at every size and both
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
#include <glm/gtx/norm.hpp>

#include <benchmark/benchmark.h>

#include "fixed.hpp"

namespace la = cheatah::linalg;

namespace {

// ---- deterministic, well-conditioned inputs, identical on both sides ---------------------------

/// A cheatah Fixed vector/matrix filled with `base + index`, plus a diagonal boost for matrices so
/// the matrix is non-singular and `inverse` is meaningful.
template <class L>
L lfill(double base) {
    L v;
    using T = typename L::value_type;
    for (std::size_t i = 0; i < L::size; ++i) { v.data()[i] = static_cast<T>(base + double(i)); }
    if constexpr (L::rank == 2) {
        for (std::size_t i = 0; i < L::rows; ++i) { v(i, i) += static_cast<T>(10 * L::rows); }
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
VEC_BENCH(vdot, la::dot(a, b), glm::dot(a, b))
VEC_BENCH(vlen, la::norm(a), glm::length(a))
VEC_BENCH(vlen2, la::squared_norm(a), glm::length2(a))
VEC_BENCH(vnorm, la::normalize(a), glm::normalize(a))
VEC_BENCH(vcross, la::cross(a, b), glm::cross(a, b))

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

PAIR_VEC_COMMON(la::vec2f, glm::vec2, "vec2f")
PAIR_VEC_COMMON(la::vec3f, glm::vec3, "vec3f")
PAIR_VEC_COMMON(la::vec4f, glm::vec4, "vec4f")
PAIR_VEC_COMMON(la::vec2d, glm::dvec2, "vec2d")
PAIR_VEC_COMMON(la::vec3d, glm::dvec3, "vec3d")
PAIR_VEC_COMMON(la::vec4d, glm::dvec4, "vec4d")

BENCHMARK(bm_vcross_fixed<la::vec3f>)->Name("BM_cross_vec3f_fixed");
BENCHMARK(bm_vcross_glm<glm::vec3>)->Name("BM_cross_vec3f_glm");
BENCHMARK(bm_vcross_fixed<la::vec3d>)->Name("BM_cross_vec3d_fixed");
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
MAT_BENCH(mmul, la::matmul(a, b), a* b)
MAT_BENCH(mtrans, la::transpose(a), glm::transpose(a))
MAT_BENCH(mdet, la::determinant(a), glm::determinant(a))
MAT_BENCH(minv, la::inverse(a), glm::inverse(a))

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

PAIR_MAT(la::mat2f, glm::mat2, la::vec2f, glm::vec2, "mat2f")
PAIR_MAT(la::mat3f, glm::mat3, la::vec3f, glm::vec3, "mat3f")
PAIR_MAT(la::mat4f, glm::mat4, la::vec4f, glm::vec4, "mat4f")
PAIR_MAT(la::mat2d, glm::dmat2, la::vec2d, glm::dvec2, "mat2d")
PAIR_MAT(la::mat3d, glm::dmat3, la::vec3d, glm::dvec3, "mat3d")
PAIR_MAT(la::mat4d, glm::dmat4, la::vec4d, glm::dvec4, "mat4d")

}  // namespace
