// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// ec_core.hpp — the width-generic short-Weierstrass ECDSA machinery shared by the `p256`
// and `p384` modules. NOT a cheatah module itself: an internal implementation header the
// two curve .cpp files include (purrc resolves modules by `<module>.hpp` name only, so
// this file is invisible to `import`).
//
// Everything is templated on a WeierstrassCurve traits struct carrying the limb count and
// the curve constants; the field/scalar Montgomery contexts are still DERIVED from the
// modulus at startup (no hand-transcribed Montgomery magic). A value is uint64_t[kLimbs],
// LEAST-significant limb first; points are Jacobian (X:Y:Z) with the curve a = -3 (true
// of every NIST prime curve). The algorithms are limb-count-independent copies of the
// battle-tested p256 versions — only bounds changed, from 4/256/32 to
// kLimbs/kBits/kBytes.

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

namespace cheatah::ec {

using u64 = std::uint64_t;
using u128 = unsigned __int128;

// The curve-traits concept every template below is constrained by: a NIST-style
// short-Weierstrass curve (a = -3) over a prime field, its size in 64-bit limbs plus the
// field prime P, group order N, coefficient B and base point (GX, GY) as little-endian
// limb arrays.
template <class C>
concept WeierstrassCurve = requires {
    { C::kLimbs } -> std::convertible_to<std::size_t>;
    requires C::kLimbs >= 4 && C::kLimbs <= 8;
    requires std::same_as<std::remove_cvref_t<decltype(C::P)>, std::array<u64, C::kLimbs>>;
    requires std::same_as<std::remove_cvref_t<decltype(C::N)>, std::array<u64, C::kLimbs>>;
    requires std::same_as<std::remove_cvref_t<decltype(C::B)>, std::array<u64, C::kLimbs>>;
    requires std::same_as<std::remove_cvref_t<decltype(C::GX)>, std::array<u64, C::kLimbs>>;
    requires std::same_as<std::remove_cvref_t<decltype(C::GY)>, std::array<u64, C::kLimbs>>;
};

template <WeierstrassCurve C>
using fe = std::array<u64, C::kLimbs>;  // one field/scalar value, limb[0] = least significant

template <WeierstrassCurve C>
inline constexpr std::size_t kBits = C::kLimbs * 64;  // scalar size in bits (256 / 384)
template <WeierstrassCurve C>
inline constexpr std::size_t kBytes = C::kLimbs * 8;  // big-endian byte size (32 / 48)

// ---- plain multi-limb helpers -----------------------------------------------------------------
template <WeierstrassCurve C>
bool is_zero(const fe<C>& a) {
    u64 acc = 0;
    for (const u64 limb : a) acc |= limb;
    return acc == 0;
}
template <WeierstrassCurve C>
bool geq(const fe<C>& a, const fe<C>& b) {  // a >= b
    for (int i = static_cast<int>(C::kLimbs) - 1; i >= 0; --i)
        if (a[i] != b[i]) return a[i] > b[i];
    return true;
}
// a - b (mod 2^kBits), returns borrow.
template <WeierstrassCurve C>
u64 sub_borrow(fe<C>& r, const fe<C>& a, const fe<C>& b) {
    u128 br = 0;
    for (std::size_t i = 0; i < C::kLimbs; ++i) {
        u128 d = (u128)a[i] - b[i] - br;
        r[i] = (u64)d;
        br = (d >> 64) & 1;
    }
    return (u64)br;
}
// a + b (mod 2^kBits), returns carry.
template <WeierstrassCurve C>
u64 add_carry(fe<C>& r, const fe<C>& a, const fe<C>& b) {
    u128 c = 0;
    for (std::size_t i = 0; i < C::kLimbs; ++i) {
        u128 s = (u128)a[i] + b[i] + c;
        r[i] = (u64)s;
        c = s >> 64;
    }
    return (u64)c;
}

// ---- Montgomery context for one modulus (R = 2^kBits) ------------------------------------------
/// Montgomery context for one modulus (R = 2^kBits) — all constants derived at startup.
template <WeierstrassCurve C>
struct Mont {
    fe<C> m;    ///< the modulus
    fe<C> rr;   ///< R^2 mod m
    fe<C> one;  ///< R mod m (Montgomery form of 1)
    u64 n0;     ///< -m^{-1} mod 2^64
};

// CIOS Montgomery multiplication: r = a*b*R^-1 mod m.
template <WeierstrassCurve C>
void mont_mul(fe<C>& r, const fe<C>& a, const fe<C>& b, const Mont<C>& M) {
    constexpr std::size_t L = C::kLimbs;
    u64 t[L + 1] = {};
    for (std::size_t i = 0; i < L; ++i) {
        // t += a * b[i]
        u128 carry = 0;
        for (std::size_t j = 0; j < L; ++j) {
            u128 p = (u128)a[j] * b[i] + t[j] + carry;
            t[j] = (u64)p;
            carry = p >> 64;
        }
        u128 s = (u128)t[L] + carry;
        t[L] = (u64)s;
        u64 top = (u64)(s >> 64);
        // m_mul = t[0] * n0 mod 2^64; t += m_mul * m; then shift right one limb
        u64 mmul = (u64)((u128)t[0] * M.n0);
        carry = 0;
        {
            u128 p = (u128)mmul * M.m[0] + t[0];
            carry = p >> 64;  // low limb becomes 0
        }
        for (std::size_t j = 1; j < L; ++j) {
            u128 p = (u128)mmul * M.m[j] + t[j] + carry;
            t[j - 1] = (u64)p;
            carry = p >> 64;
        }
        u128 s2 = (u128)t[L] + carry;
        t[L - 1] = (u64)s2;
        t[L] = top + (u64)(s2 >> 64);
    }
    fe<C> res;
    for (std::size_t i = 0; i < L; ++i) res[i] = t[i];
    // final conditional subtraction (t may be in [0, 2m))
    if (t[L] != 0 || geq<C>(res, M.m)) {
        fe<C> tmp;
        sub_borrow<C>(tmp, res, M.m);
        res = tmp;
    }
    r = res;
}
template <WeierstrassCurve C>
void mont_add(fe<C>& r, const fe<C>& a, const fe<C>& b, const Mont<C>& M) {
    fe<C> s;
    u64 c = add_carry<C>(s, a, b);
    if (c || geq<C>(s, M.m)) {
        fe<C> t;
        sub_borrow<C>(t, s, M.m);
        s = t;
    }
    r = s;
}
template <WeierstrassCurve C>
void mont_sub(fe<C>& r, const fe<C>& a, const fe<C>& b, const Mont<C>& M) {
    fe<C> d;
    u64 br = sub_borrow<C>(d, a, b);
    if (br) {
        fe<C> t;
        add_carry<C>(t, d, M.m);
        d = t;
    }
    r = d;
}
template <WeierstrassCurve C>
void to_mont(fe<C>& r, const fe<C>& a, const Mont<C>& M) {
    mont_mul<C>(r, a, M.rr, M);
}
template <WeierstrassCurve C>
void from_mont(fe<C>& r, const fe<C>& a, const Mont<C>& M) {
    fe<C> one{};
    one[0] = 1;
    mont_mul<C>(r, a, one, M);
}
// r = a^-1 mod m, via Fermat: a^(m-2). (m is prime for both p and n.)
template <WeierstrassCurve C>
void mont_inv(fe<C>& r, const fe<C>& a, const Mont<C>& M) {
    fe<C> two{};
    two[0] = 2;
    fe<C> exp;
    sub_borrow<C>(exp, M.m, two);  // m - 2
    fe<C> result = M.one;          // Montgomery 1
    fe<C> base = a;
    for (std::size_t i = 0; i < kBits<C>; ++i) {
        if ((exp[i / 64] >> (i % 64)) & 1) mont_mul<C>(result, result, base, M);
        mont_mul<C>(base, base, base, M);
    }
    r = result;
}

inline u64 inv64(u64 a) {  // a^-1 mod 2^64 (a odd), Newton's iteration
    u64 x = a;             // correct to 3 bits
    for (int i = 0; i < 5; ++i) x *= 2 - a * x;
    return x;
}
template <WeierstrassCurve C>
Mont<C> make_mont(const fe<C>& m) {
    Mont<C> M;
    M.m = m;
    M.n0 = 0 - inv64(m[0]);
    // rr = 2^(2*kBits) mod m, by 2*kBits doublings of 1 with conditional subtract.
    fe<C> x{};
    x[0] = 1;
    for (std::size_t i = 0; i < 2 * kBits<C>; ++i) {
        fe<C> d;
        u64 c = add_carry<C>(d, x, x);
        if (c || geq<C>(d, m)) {
            fe<C> t;
            sub_borrow<C>(t, d, m);
            d = t;
        }
        x = d;
    }
    M.rr = x;
    // one = R mod m = 2^kBits mod m  -> to_mont(1)
    fe<C> oneN{};
    oneN[0] = 1;
    mont_mul<C>(M.one, oneN, M.rr, M);
    return M;
}

// ---- the two per-curve field contexts (built once per instantiation) ---------------------------
template <WeierstrassCurve C>
const Mont<C>& Fp() {
    static const Mont<C> m = make_mont<C>(C::P);
    return m;
}
template <WeierstrassCurve C>
const Mont<C>& Fn() {
    static const Mont<C> m = make_mont<C>(C::N);
    return m;
}

// ---- bytes <-> limbs (kBytes big-endian bytes) --------------------------------------------------
template <WeierstrassCurve C>
fe<C> be_to_fe(const unsigned char* b) {
    fe<C> r{};
    for (std::size_t limb = 0; limb < C::kLimbs; ++limb) {
        u64 v = 0;
        const unsigned char* p = b + (C::kLimbs - 1 - limb) * 8;  // most-significant 8 bytes last
        for (int k = 0; k < 8; ++k) v = (v << 8) | p[k];
        r[limb] = v;
    }
    return r;
}
template <WeierstrassCurve C>
void fe_to_be(unsigned char* out, const fe<C>& a) {
    for (std::size_t limb = 0; limb < C::kLimbs; ++limb) {
        u64 v = a[limb];
        unsigned char* p = out + (C::kLimbs - 1 - limb) * 8;
        for (int k = 7; k >= 0; --k) {
            p[k] = (unsigned char)(v & 0xFF);
            v >>= 8;
        }
    }
}

// ---- Jacobian points (coordinates in Montgomery form, mod p) ------------------------------------
/// A curve point in Jacobian projective coordinates (x = X/Z^2, y = Y/Z^3), Montgomery form.
template <WeierstrassCurve C>
struct Jac {
    fe<C> X;   ///< projective X
    fe<C> Y;   ///< projective Y
    fe<C> Z;   ///< projective Z (0 also encodes the point at infinity)
    bool inf;  ///< explicit point-at-infinity flag
};
template <WeierstrassCurve C>
Jac<C> jac_infinity() {
    return Jac<C>{Fp<C>().one, Fp<C>().one, fe<C>{}, true};
}

template <WeierstrassCurve C>
void jac_double(Jac<C>& r, const Jac<C>& q) {
    const Mont<C>& F = Fp<C>();
    if (q.inf || is_zero<C>(q.Z)) {
        r = jac_infinity<C>();
        return;
    }
    fe<C> A, B, Cc, D, t1, t2;
    mont_mul<C>(A, q.X, q.X, F);  // X^2
    mont_mul<C>(B, q.Y, q.Y, F);  // Y^2
    mont_mul<C>(Cc, B, B, F);     // Y^4
    // D = 2*((X+B)^2 - A - C)
    mont_add<C>(t1, q.X, B, F);
    mont_mul<C>(t1, t1, t1, F);
    mont_sub<C>(t1, t1, A, F);
    mont_sub<C>(t1, t1, Cc, F);
    mont_add<C>(D, t1, t1, F);
    // ZZ = Z^2 ; E = 3*(X - ZZ)*(X + ZZ)  [uses a = -3]
    fe<C> ZZ;
    mont_mul<C>(ZZ, q.Z, q.Z, F);
    mont_sub<C>(t1, q.X, ZZ, F);
    mont_add<C>(t2, q.X, ZZ, F);
    mont_mul<C>(t1, t1, t2, F);
    fe<C> E;
    mont_add<C>(E, t1, t1, F);
    mont_add<C>(E, E, t1, F);  // 3*(...)
    // F2 = E^2 ; X3 = F2 - 2D
    fe<C> X3;
    mont_mul<C>(X3, E, E, F);
    mont_sub<C>(X3, X3, D, F);
    mont_sub<C>(X3, X3, D, F);
    // Y3 = E*(D - X3) - 8C
    fe<C> Y3, eight;
    mont_sub<C>(t1, D, X3, F);
    mont_mul<C>(Y3, E, t1, F);
    mont_add<C>(eight, Cc, Cc, F);
    mont_add<C>(eight, eight, eight, F);
    mont_add<C>(eight, eight, eight, F);  // 8C
    mont_sub<C>(Y3, Y3, eight, F);
    // Z3 = 2*Y*Z
    fe<C> Z3;
    mont_mul<C>(Z3, q.Y, q.Z, F);
    mont_add<C>(Z3, Z3, Z3, F);
    r = Jac<C>{X3, Y3, Z3, false};
}

template <WeierstrassCurve C>
void jac_add(Jac<C>& r, const Jac<C>& a, const Jac<C>& b) {
    const Mont<C>& F = Fp<C>();
    if (a.inf || is_zero<C>(a.Z)) {
        r = b;
        return;
    }
    if (b.inf || is_zero<C>(b.Z)) {
        r = a;
        return;
    }
    fe<C> Z1Z1, Z2Z2, U1, U2, S1, S2;
    mont_mul<C>(Z1Z1, a.Z, a.Z, F);
    mont_mul<C>(Z2Z2, b.Z, b.Z, F);
    mont_mul<C>(U1, a.X, Z2Z2, F);
    mont_mul<C>(U2, b.X, Z1Z1, F);
    fe<C> t;
    mont_mul<C>(t, b.Z, Z2Z2, F);
    mont_mul<C>(S1, a.Y, t, F);
    mont_mul<C>(t, a.Z, Z1Z1, F);
    mont_mul<C>(S2, b.Y, t, F);
    fe<C> H, Rr;
    mont_sub<C>(H, U2, U1, F);
    mont_sub<C>(Rr, S2, S1, F);
    if (is_zero<C>(H)) {
        if (is_zero<C>(Rr)) {
            jac_double<C>(r, a);
            return;
        }
        r = jac_infinity<C>();
        return;
    }
    fe<C> HH, HHH, V;
    mont_mul<C>(HH, H, H, F);
    mont_mul<C>(HHH, HH, H, F);
    mont_mul<C>(V, U1, HH, F);
    fe<C> X3;
    mont_mul<C>(X3, Rr, Rr, F);
    mont_sub<C>(X3, X3, HHH, F);
    mont_sub<C>(X3, X3, V, F);
    mont_sub<C>(X3, X3, V, F);
    fe<C> Y3;
    mont_sub<C>(t, V, X3, F);
    mont_mul<C>(Y3, Rr, t, F);
    fe<C> s1hhh;
    mont_mul<C>(s1hhh, S1, HHH, F);
    mont_sub<C>(Y3, Y3, s1hhh, F);
    fe<C> Z3;
    mont_mul<C>(Z3, a.Z, b.Z, F);
    mont_mul<C>(Z3, Z3, H, F);
    r = Jac<C>{X3, Y3, Z3, false};
}

// Strauss-Shamir: u1*A + u2*B with ONE doubling chain (kBits doublings total)
// instead of two separate scalar multiplications. A 2-bit window over both
// scalars uses a 16-entry combined table [i*A + j*B] so it also halves the adds.
template <WeierstrassCurve C>
void jac_double_mul(Jac<C>& r, const fe<C>& u1, const Jac<C>& A, const fe<C>& u2, const Jac<C>& B) {
    Jac<C> tbl[4][4];  // tbl[i][j] = i*A + j*B, i,j in {0..3}
    tbl[0][0] = jac_infinity<C>();
    tbl[1][0] = A;
    jac_double<C>(tbl[2][0], A);
    jac_add<C>(tbl[3][0], tbl[2][0], A);
    tbl[0][1] = B;
    jac_double<C>(tbl[0][2], B);
    jac_add<C>(tbl[0][3], tbl[0][2], B);
    for (int i = 1; i < 4; ++i)
        for (int j = 1; j < 4; ++j) jac_add<C>(tbl[i][j], tbl[i][0], tbl[0][j]);

    Jac<C> acc = jac_infinity<C>();
    for (int i = static_cast<int>(kBits<C>) - 2; i >= 0; i -= 2) {  // kBits is even
        Jac<C> t;
        jac_double<C>(t, acc);
        acc = t;
        jac_double<C>(t, acc);
        acc = t;
        const unsigned a = (u1[i / 64] >> (i % 64)) & 0x3;
        const unsigned b = (u2[i / 64] >> (i % 64)) & 0x3;
        if (a || b) {
            jac_add<C>(t, acc, tbl[a][b]);
            acc = t;
        }
    }
    r = acc;
}

// affine x-coordinate (normal form) of a Jacobian point: x = X / Z^2.
template <WeierstrassCurve C>
fe<C> jac_affine_x(const Jac<C>& q) {
    const Mont<C>& F = Fp<C>();
    fe<C> zinv, zinv2, x;
    mont_inv<C>(zinv, q.Z, F);
    mont_mul<C>(zinv2, zinv, zinv, F);
    mont_mul<C>(x, q.X, zinv2, F);
    fe<C> out;
    from_mont<C>(out, x, F);
    return out;
}

template <WeierstrassCurve C>
Jac<C> affine_to_jac(const fe<C>& x, const fe<C>& y) {
    const Mont<C>& F = Fp<C>();
    Jac<C> p;
    to_mont<C>(p.X, x, F);
    to_mont<C>(p.Y, y, F);
    p.Z = F.one;
    p.inf = false;
    return p;
}
template <WeierstrassCurve C>
const Jac<C>& base_point() {
    static const Jac<C> g = affine_to_jac<C>(C::GX, C::GY);
    return g;
}

// Fixed-base comb for k*G. G is constant, so we precompute (once) the 2^kLimbs-entry
// table T[s] = sum over set bits i of s of (2^(64*i) * G). Then k*G is just 64
// doublings + 64 adds (vs kBits doublings for a generic window) — the big win for
// the per-message signing path. Selector at step j is bit j of each 64-bit limb.
template <WeierstrassCurve C>
const std::array<Jac<C>, (1u << C::kLimbs)>& g_comb() {
    static const std::array<Jac<C>, (1u << C::kLimbs)> tbl = [] {
        constexpr std::size_t L = C::kLimbs;
        Jac<C> gi[L];
        gi[0] = affine_to_jac<C>(C::GX, C::GY);
        for (std::size_t i = 1; i < L; ++i) {
            Jac<C> acc = gi[i - 1];
            for (int b = 0; b < 64; ++b) {  // gi[i] = 2^64 * gi[i-1]
                Jac<C> t;
                jac_double<C>(t, acc);
                acc = t;
            }
            gi[i] = acc;
        }
        std::array<Jac<C>, (1u << L)> t;
        t[0] = jac_infinity<C>();
        for (unsigned s = 1; s < (1u << L); ++s) {
            Jac<C> acc = jac_infinity<C>();
            for (std::size_t i = 0; i < L; ++i)
                if (s & (1u << i)) {
                    Jac<C> r;
                    jac_add<C>(r, acc, gi[i]);
                    acc = r;
                }
            t[s] = acc;
        }
        return t;
    }();
    return tbl;
}

template <WeierstrassCurve C>
void jac_mul_base(Jac<C>& r, const fe<C>& k) {
    const auto& T = g_comb<C>();
    Jac<C> acc = jac_infinity<C>();
    for (int j = 63; j >= 0; --j) {
        Jac<C> t;
        jac_double<C>(t, acc);
        acc = t;
        unsigned sel = 0;
        for (std::size_t i = 0; i < C::kLimbs; ++i) sel |= ((k[i] >> j) & 1u) << i;
        if (sel) {
            jac_add<C>(t, acc, T[sel]);
            acc = t;
        }
    }
    r = acc;
}

// Reduce a scalar already known to be < 2n into [0, n): a single conditional
// subtraction of the group order n. Used for the FIPS 186-4 hash truncation and
// for folding a curve x-coordinate (which lives in [0, p) < 2n) into a scalar.
template <WeierstrassCurve C>
fe<C> reduce_mod_n(const fe<C>& v) {
    if (geq<C>(v, C::N)) {
        fe<C> t;
        sub_borrow<C>(t, v, C::N);
        return t;
    }
    return v;
}

// Reduce a big-endian hash to a scalar in [0, n). A hash of at least kBytes keeps its
// leftmost kBytes (the FIPS 186-4 leftmost-bits truncation); a SHORTER hash is the whole
// value (X9.62 bits2int — right-aligned), e.g. a SHA-256 signature under a P-384 key.
template <WeierstrassCurve C>
fe<C> hash_to_scalar(const std::string& h) {
    unsigned char buf[kBytes<C>] = {0};
    if (h.size() >= kBytes<C>)
        std::memcpy(buf, h.data(), kBytes<C>);
    else
        std::memcpy(buf + (kBytes<C> - h.size()), h.data(), h.size());
    return reduce_mod_n<C>(be_to_fe<C>(buf));
}

// ---- minimal DER helpers ------------------------------------------------------------------------
// Parse SEQUENCE{INTEGER r, INTEGER s} -> kBytes big-endian r and s. false on error.
// Short-form lengths only: both curves' SEQUENCE stays under 128 bytes (P-384: <= ~104).
template <WeierstrassCurve C>
bool der_to_rs(const std::string& der, unsigned char* r, unsigned char* s) {
    const unsigned char* p = (const unsigned char*)der.data();
    std::size_t n = der.size(), i = 0;
    auto read_int = [&](unsigned char* out) -> bool {
        if (i >= n || p[i++] != 0x02) return false;
        if (i >= n) return false;
        std::size_t len = p[i++];
        if (len & 0x80) return false;  // curve-order ints are short-form
        if (i + len > n || len == 0) return false;
        const unsigned char* v = p + i;
        // strip a leading zero (sign byte)
        while (len > 1 && v[0] == 0) {
            ++v;
            --len;
        }
        if (len > kBytes<C>) return false;
        std::memset(out, 0, kBytes<C>);
        std::memcpy(out + (kBytes<C> - len), v, len);
        i += (std::size_t)(v - (p + i)) + len;  // advance past the original field
        return true;
    };
    if (i >= n || p[i++] != 0x30) return false;
    if (i >= n) return false;
    std::size_t seqlen = p[i++];
    if (seqlen & 0x80) return false;
    if (i + seqlen != n) return false;
    return read_int(r) && read_int(s);
}

// Is (x, y) on the curve y^2 = x^3 - 3x + b (mod p)? Rejects an off-curve /
// invalid-curve public key — SP 800-56A / FIPS 186 point validation, which the plain
// coordinate-range check (x,y < p) does not catch.
template <WeierstrassCurve C>
bool on_curve(const fe<C>& x, const fe<C>& y) {
    const Mont<C>& F = Fp<C>();
    fe<C> xm, ym, x2, x3, tx, rhs, bm, y2;
    to_mont<C>(xm, x, F);
    to_mont<C>(ym, y, F);
    mont_mul<C>(x2, xm, xm, F);    // x^2
    mont_mul<C>(x3, x2, xm, F);    // x^3
    mont_add<C>(tx, xm, xm, F);    // 2x
    mont_add<C>(tx, tx, xm, F);    // 3x
    mont_sub<C>(rhs, x3, tx, F);   // x^3 - 3x
    to_mont<C>(bm, C::B, F);
    mont_add<C>(rhs, rhs, bm, F);  // x^3 - 3x + b
    mont_mul<C>(y2, ym, ym, F);    // y^2
    return std::memcmp(y2.data(), rhs.data(), sizeof(fe<C>)) == 0;
}

// ECDSA verification over raw byte forms: pubkey = 2*kBytes X||Y, sig = 2*kBytes r||s.
template <WeierstrassCurve C>
bool verify_raw(const std::string& pubkey_xy, const std::string& msg_hash,
                const std::string& sig_raw) {
    if (pubkey_xy.size() != 2 * kBytes<C> || sig_raw.size() != 2 * kBytes<C>) return false;
    fe<C> r = be_to_fe<C>((const unsigned char*)sig_raw.data());
    fe<C> s = be_to_fe<C>((const unsigned char*)sig_raw.data() + kBytes<C>);
    if (is_zero<C>(r) || is_zero<C>(s) || geq<C>(r, C::N) || geq<C>(s, C::N)) return false;

    const Mont<C>& Fnn = Fn<C>();
    fe<C> e = hash_to_scalar<C>(msg_hash);
    fe<C> sm, em, rm, w, u1, u2;
    to_mont<C>(sm, s, Fnn);
    mont_inv<C>(w, sm, Fnn);  // w = s^-1 (Montgomery)
    to_mont<C>(em, e, Fnn);
    to_mont<C>(rm, r, Fnn);
    fe<C> u1m, u2m;
    mont_mul<C>(u1m, em, w, Fnn);
    mont_mul<C>(u2m, rm, w, Fnn);
    from_mont<C>(u1, u1m, Fnn);
    from_mont<C>(u2, u2m, Fnn);

    fe<C> qx = be_to_fe<C>((const unsigned char*)pubkey_xy.data());
    fe<C> qy = be_to_fe<C>((const unsigned char*)pubkey_xy.data() + kBytes<C>);
    if (geq<C>(qx, C::P) || geq<C>(qy, C::P)) return false;
    if (!on_curve<C>(qx, qy)) return false;  // reject off-curve / invalid-curve public keys
    Jac<C> Q = affine_to_jac<C>(qx, qy);

    Jac<C> R;
    jac_double_mul<C>(R, u1, base_point<C>(), u2, Q);  // u1*G + u2*Q, one doubling chain
    if (R.inf || is_zero<C>(R.Z)) return false;
    fe<C> x = reduce_mod_n<C>(jac_affine_x<C>(R));
    return std::memcmp(x.data(), r.data(), sizeof(fe<C>)) == 0;
}

// ECDSA verification of the DER form (SEQUENCE{INTEGER r, INTEGER s} — TLS/X.509).
template <WeierstrassCurve C>
bool verify_der(const std::string& pubkey_xy, const std::string& msg_hash,
                const std::string& sig_der) {
    unsigned char r[kBytes<C>], s[kBytes<C>];
    if (!der_to_rs<C>(sig_der, r, s)) return false;
    std::string raw;
    raw.resize(2 * kBytes<C>);
    std::memcpy(raw.data(), r, kBytes<C>);
    std::memcpy(raw.data() + kBytes<C>, s, kBytes<C>);
    return verify_raw<C>(pubkey_xy, msg_hash, raw);
}

}  // namespace cheatah::ec
