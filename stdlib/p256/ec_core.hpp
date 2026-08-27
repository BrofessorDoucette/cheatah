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
/**
 * Whether every limb of @p a is zero (an OR-accumulate over all limbs, no early exit).
 * @tparam C the curve traits.
 * @param a the value to test.
 * @return true iff @p a == 0.
 * @complexity O(1) — kLimbs limb reads on a fixed-width value.
 * @alloc none.
 * @test CheatahP256.VerifyRejectsOutOfRangeAndInfinity
 */
template <WeierstrassCurve C>
bool is_zero(const fe<C>& a) {
    u64 acc = 0;
    for (const u64 limb : a) acc |= limb;
    return acc == 0;
}
/**
 * Branch-free boolean-to-mask: false -> 0, true -> all-ones.
 * @param c the condition.
 * @return the 64-bit mask.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahP256.ConstantTimePointOpsMatchReference
 */
inline u64 ct_mask(bool c) { return u64(0) - static_cast<u64>(c); }

/**
 * Constant-time conditional move over a field element: r = m ? a : r, per limb, no branch.
 * @tparam C the curve traits.
 * @param r the destination (kept when @p m is 0).
 * @param a the source (copied when @p m is all-ones).
 * @param m the ct_mask (0 or all-ones).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahP256.ConstantTimePointOpsMatchReference
 */
template <WeierstrassCurve C>
inline void fe_cmov(fe<C>& r, const fe<C>& a, u64 m) {
    for (std::size_t i = 0; i < C::kLimbs; ++i) r[i] = (r[i] & ~m) | (a[i] & m);
}

/**
 * r = a - b (mod 2^kBits), returns the borrow.
 * @tparam C the curve traits.
 * @param r receives the difference.
 * @param a minuend.
 * @param b subtrahend.
 * @return the final borrow: 1 iff a < b, else 0.
 * @complexity O(1) — one pass over kLimbs limbs.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
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
/**
 * r = a + b (mod 2^kBits), returns the carry.
 * @tparam C the curve traits.
 * @param r receives the sum.
 * @param a first addend.
 * @param b second addend.
 * @return the final carry out of the top limb (0 or 1).
 * @complexity O(1) — one pass over kLimbs limbs.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
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

/**
 * Multi-limb unsigned compare, in constant time.
 * @tparam C the curve traits.
 * @param a left operand.
 * @param b right operand.
 * @return true iff a >= b.
 * @complexity O(1) — one full-width subtract, always every limb.
 * @alloc none.
 * @test CheatahP256.VerifyRejectsOutOfRangeAndInfinity
 */
template <WeierstrassCurve C>
bool geq(const fe<C>& a, const fe<C>& b) {  // a >= b
    // A full-width subtract, not a most-significant-limb-first scan. The scan returned at the
    // FIRST differing limb, so its running time revealed how many high limbs the two shared —
    // and this is called directly on the private key and on the RFC 6979 nonce. `a >= b` is
    // exactly "the subtraction did not borrow", and sub_borrow is already branch-free.
    fe<C> scratch{};
    return sub_borrow<C>(scratch, a, b) == 0;
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

/**
 * CIOS Montgomery multiplication: r = a*b*R^-1 mod m.
 * @tparam C the curve traits.
 * @param r receives the product.
 * @param a first factor (Montgomery form).
 * @param b second factor (Montgomery form).
 * @param M the Montgomery context.
 * @complexity O(1) — kLimbs^2 limb multiplies on a fixed-width value.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
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
    // Final conditional subtraction (t may be in [0, 2m)) — computed ALWAYS and selected with a
    // mask. The subtraction's own borrow is the comparison: no borrow means res >= m. The old
    // form branched, and its `||` short-circuited, so the top-word case skipped the compare
    // entirely — two data-dependent timing signals in the hot loop of every secret-scalar
    // multiply.
    fe<C> reduced;
    const u64 borrow = sub_borrow<C>(reduced, res, M.m);
    fe_cmov<C>(res, reduced, ~ct_mask(borrow != 0) | ct_mask(t[L] != 0));
    r = res;
}
/**
 * Modular addition: r = a + b mod m (add, then one conditional subtract of m).
 * @tparam C the curve traits.
 * @param r receives the sum.
 * @param a first addend.
 * @param b second addend.
 * @param M the Montgomery context (only its modulus is used).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
template <WeierstrassCurve C>
void mont_add(fe<C>& r, const fe<C>& a, const fe<C>& b, const Mont<C>& M) {
    fe<C> s;
    const u64 c = add_carry<C>(s, a, b);
    // Subtract m when the sum overflowed (c) or when it is already >= m — the latter being
    // exactly "the subtraction did not borrow". Both branches computed, one selected.
    fe<C> reduced;
    const u64 borrow = sub_borrow<C>(reduced, s, M.m);
    fe_cmov<C>(s, reduced, ct_mask(c != 0) | ~ct_mask(borrow != 0));
    r = s;
}
/**
 * Modular subtraction: r = a - b mod m (subtract, then one conditional add of m on borrow).
 * @tparam C the curve traits.
 * @param r receives the difference.
 * @param a minuend.
 * @param b subtrahend.
 * @param M the Montgomery context (only its modulus is used).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
template <WeierstrassCurve C>
void mont_sub(fe<C>& r, const fe<C>& a, const fe<C>& b, const Mont<C>& M) {
    fe<C> d;
    const u64 br = sub_borrow<C>(d, a, b);
    // Add m back on a borrow — computed unconditionally, selected by the borrow's mask.
    fe<C> wrapped;
    add_carry<C>(wrapped, d, M.m);
    fe_cmov<C>(d, wrapped, ct_mask(br != 0));
    r = d;
}
/**
 * Convert @p a into Montgomery form: r = a*R mod m (one mont_mul by R^2).
 * @tparam C the curve traits.
 * @param r receives the Montgomery form.
 * @param a the plain value.
 * @param M the Montgomery context.
 * @complexity O(1) — one mont_mul.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
template <WeierstrassCurve C>
void to_mont(fe<C>& r, const fe<C>& a, const Mont<C>& M) {
    mont_mul<C>(r, a, M.rr, M);
}
/**
 * Convert @p a out of Montgomery form: r = a*R^-1 mod m (one mont_mul by 1).
 * @tparam C the curve traits.
 * @param r receives the plain value.
 * @param a the Montgomery-form value.
 * @param M the Montgomery context.
 * @complexity O(1) — one mont_mul.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
template <WeierstrassCurve C>
void from_mont(fe<C>& r, const fe<C>& a, const Mont<C>& M) {
    fe<C> one{};
    one[0] = 1;
    mont_mul<C>(r, a, one, M);
}
/**
 * r = a^-1 mod m, via Fermat: a^(m-2). (m is prime for both p and n.)
 * @tparam C the curve traits.
 * @param r receives the inverse (Montgomery form).
 * @param a the value to invert (Montgomery form, nonzero).
 * @param M the Montgomery context.
 * @complexity O(1) — a fixed kBits-step square-and-multiply ladder.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
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

/**
 * a^-1 mod 2^64 (@p a odd), by Newton's iteration.
 * @param a the odd value to invert.
 * @return the inverse mod 2^64.
 * @complexity O(1) — five fixed Newton steps.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
inline u64 inv64(u64 a) {  // starts correct to 3 bits, doubles per step
    u64 x = a;             // correct to 3 bits
    for (int i = 0; i < 5; ++i) x *= 2 - a * x;
    return x;
}
/**
 * Build the Montgomery context for modulus @p m — every constant (n0, R^2 mod m, R mod m)
 * derived at startup, no hand-transcribed Montgomery magic.
 * @tparam C the curve traits.
 * @param m the (odd, prime) modulus.
 * @return the derived context.
 * @complexity O(1) — 2*kBits fixed doubling steps to derive R^2 mod m.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
template <WeierstrassCurve C>
Mont<C> make_mont(const fe<C>& m) {
    Mont<C> M{};
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
/**
 * The curve's field context: the Montgomery context for the prime P, built once per
 * instantiation (function-local static).
 * @tparam C the curve traits.
 * @return the context mod C::P.
 * @complexity O(1) after the one-time static make_mont on first use.
 * @alloc none — static storage.
 * @test CheatahP256.VerifyKnownVector
 */
template <WeierstrassCurve C>
const Mont<C>& Fp() {
    static const Mont<C> m = make_mont<C>(C::P);
    return m;
}
/**
 * The curve's scalar context: the Montgomery context for the group order N, built once per
 * instantiation (function-local static).
 * @tparam C the curve traits.
 * @return the context mod C::N.
 * @complexity O(1) after the one-time static make_mont on first use.
 * @alloc none — static storage.
 * @test CheatahP256.VerifyKnownVector
 */
template <WeierstrassCurve C>
const Mont<C>& Fn() {
    static const Mont<C> m = make_mont<C>(C::N);
    return m;
}

// ---- bytes <-> limbs (kBytes big-endian bytes) --------------------------------------------------
/**
 * Load kBytes big-endian bytes into a little-endian limb array.
 * @tparam C the curve traits.
 * @param b pointer to kBytes bytes, most significant first.
 * @return the value.
 * @complexity O(1) — kBytes byte reads.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
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
/**
 * Store a limb array as kBytes big-endian bytes (the inverse of be_to_fe).
 * @tparam C the curve traits.
 * @param out receives kBytes bytes, most significant first.
 * @param a the value to serialize.
 * @complexity O(1) — kBytes byte writes.
 * @alloc none.
 * @test CheatahP256.SignKnownVector
 */
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
/**
 * The point at infinity (the group identity): Z = 0 with the explicit flag set.
 * @tparam C the curve traits.
 * @return the identity point.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahP256.VerifyHitsGroupLawSpecialCases
 */
template <WeierstrassCurve C>
Jac<C> jac_infinity() {
    return Jac<C>{Fp<C>().one, Fp<C>().one, fe<C>{}, true};
}

/**
 * Jacobian point doubling, r = 2q, using the a = -3 formulas (true of every NIST prime curve).
 * Branchy (early-returns on infinity): for PUBLIC data only — the secret path uses jac_double_ct.
 * @tparam C the curve traits.
 * @param r receives the doubled point.
 * @param q the point to double.
 * @complexity O(1) — a fixed count of field operations.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
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

/**
 * Jacobian point addition, r = a + b, with branchy special cases (either operand infinity,
 * a == b -> double, a == -b -> infinity). For PUBLIC data only — the secret path uses jac_add_ct.
 * @tparam C the curve traits.
 * @param r receives the sum.
 * @param a first point.
 * @param b second point.
 * @complexity O(1) — a fixed count of field operations.
 * @alloc none.
 * @test CheatahP256.VerifyHitsGroupLawSpecialCases
 */
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

/**
 * Strauss-Shamir: u1*A + u2*B with ONE doubling chain (kBits doublings total)
 * instead of two separate scalar multiplications. A 2-bit window over both
 * scalars uses a 16-entry combined table [i*A + j*B] so it also halves the adds.
 * @tparam C the curve traits.
 * @param r receives u1*A + u2*B.
 * @param u1 first (public) scalar.
 * @param A first point.
 * @param u2 second (public) scalar.
 * @param B second point.
 * @complexity O(1) — kBits doublings plus at most kBits/2 adds.
 * @alloc none — the 16-entry window table lives on the stack.
 * @test CheatahP256.VerifyKnownVector
 */
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
        Jac<C> t{};
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

/**
 * The affine x-coordinate (normal form) of a Jacobian point: x = X / Z^2.
 * @tparam C the curve traits.
 * @param q the point (not infinity: Z must be invertible).
 * @return x out of Montgomery form.
 * @complexity O(1) — dominated by one mont_inv (a fixed Fermat ladder).
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
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

/**
 * Lift an affine point into Jacobian Montgomery form (Z = 1).
 * @tparam C the curve traits.
 * @param x the affine x-coordinate (plain form).
 * @param y the affine y-coordinate (plain form).
 * @return the Jacobian point.
 * @complexity O(1) — two to_mont conversions.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
template <WeierstrassCurve C>
Jac<C> affine_to_jac(const fe<C>& x, const fe<C>& y) {
    const Mont<C>& F = Fp<C>();
    Jac<C> p{};
    to_mont<C>(p.X, x, F);
    to_mont<C>(p.Y, y, F);
    p.Z = F.one;
    p.inf = false;
    return p;
}
/**
 * The curve base point G, lifted to Jacobian form once (function-local static).
 * @tparam C the curve traits.
 * @return G.
 * @complexity O(1) after the one-time static lift on first use.
 * @alloc none — static storage.
 * @test CheatahP256.VerifyKnownVector
 */
template <WeierstrassCurve C>
const Jac<C>& base_point() {
    static const Jac<C> g = affine_to_jac<C>(C::GX, C::GY);
    return g;
}

/**
 * Fixed-base comb for k*G. G is constant, so we precompute (once) the 2^kLimbs-entry
 * table T[s] = sum over set bits i of s of (2^(64*i) * G). Then k*G is just 64
 * doublings + 64 adds (vs kBits doublings for a generic window) — the big win for
 * the per-message signing path. Selector at step j is bit j of each 64-bit limb.
 * @tparam C the curve traits.
 * @return the comb table.
 * @complexity O(1) after the one-time static build ((kLimbs-1)*64 doublings plus the subset sums).
 * @alloc none — the table is a function-local static std::array.
 * @test CheatahP256.SignKnownVector
 */
template <WeierstrassCurve C>
const std::array<Jac<C>, (1u << C::kLimbs)>& g_comb() {
    static const std::array<Jac<C>, (1u << C::kLimbs)> tbl = [] {
        constexpr std::size_t L = C::kLimbs;
        Jac<C> gi[L];
        gi[0] = affine_to_jac<C>(C::GX, C::GY);
        for (std::size_t i = 1; i < L; ++i) {
            Jac<C> acc = gi[i - 1];
            for (int b = 0; b < 64; ++b) {  // gi[i] = 2^64 * gi[i-1]
                Jac<C> t{};
                jac_double<C>(t, acc);
                acc = t;
            }
            gi[i] = acc;
        }
        std::array<Jac<C>, (1u << L)> t{};
        t[0] = jac_infinity<C>();
        for (unsigned s = 1; s < (1u << L); ++s) {
            Jac<C> acc = jac_infinity<C>();
            for (std::size_t i = 0; i < L; ++i)
                if (s & (1u << i)) {
                    Jac<C> r{};
                    jac_add<C>(r, acc, gi[i]);
                    acc = r;
                }
            t[s] = acc;
        }
        return t;
    }();
    return tbl;
}

// ---- constant-time point ops for the SECRET-scalar path (signing k*G, keygen d*G) --------------
// jac_double_mul (verify) operates on PUBLIC data and stays branchy; the fixed-base comb below,
// which multiplies the secret nonce/key, must not branch or index on secret bits. These helpers
// give it branch-free doubling, addition, and table selection. They are differentially tested
// against the branchy jac_double/jac_add over general + edge inputs
// (CheatahP256.ConstantTimePointOpsMatchReference).

/**
 * Constant-time conditional move over a Jacobian point (all three coordinates via fe_cmov;
 * the inf flag is recomputed from Z, which encodes infinity throughout the CT path).
 * @tparam C the curve traits.
 * @param r the destination point.
 * @param a the source point.
 * @param m the ct_mask (0 or all-ones).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahP256.ConstantTimePointOpsMatchReference
 */
template <WeierstrassCurve C>
inline void jac_cmov(Jac<C>& r, const Jac<C>& a, u64 m) {
    fe_cmov<C>(r.X, a.X, m);
    fe_cmov<C>(r.Y, a.Y, m);
    fe_cmov<C>(r.Z, a.Z, m);
    r.inf = is_zero<C>(r.Z);  // infinity is encoded by Z==0 throughout the CT path
}

/**
 * Point doubling WITHOUT the is-infinity early return: the formula's Z3 = 2*Y*Z is already 0 when
 * the input is infinity (Z==0), so it self-encodes infinity, and a prime-order curve has no
 * finite 2-torsion point that could double TO infinity — so no branch is needed.
 * @tparam C the curve traits.
 * @param r receives 2q.
 * @param q the point to double.
 * @complexity O(1) — the same fixed field-operation count for every input.
 * @alloc none.
 * @test CheatahP256.ConstantTimePointOpsMatchReference
 */
template <WeierstrassCurve C>
void jac_double_ct(Jac<C>& r, const Jac<C>& q) {
    const Mont<C>& F = Fp<C>();
    fe<C> A, B, Cc, D, t1, t2;
    mont_mul<C>(A, q.X, q.X, F);
    mont_mul<C>(B, q.Y, q.Y, F);
    mont_mul<C>(Cc, B, B, F);
    mont_add<C>(t1, q.X, B, F);
    mont_mul<C>(t1, t1, t1, F);
    mont_sub<C>(t1, t1, A, F);
    mont_sub<C>(t1, t1, Cc, F);
    mont_add<C>(D, t1, t1, F);
    fe<C> ZZ;
    mont_mul<C>(ZZ, q.Z, q.Z, F);
    mont_sub<C>(t1, q.X, ZZ, F);
    mont_add<C>(t2, q.X, ZZ, F);
    mont_mul<C>(t1, t1, t2, F);
    fe<C> E;
    mont_add<C>(E, t1, t1, F);
    mont_add<C>(E, E, t1, F);
    fe<C> X3;
    mont_mul<C>(X3, E, E, F);
    mont_sub<C>(X3, X3, D, F);
    mont_sub<C>(X3, X3, D, F);
    fe<C> Y3, eight;
    mont_sub<C>(t1, D, X3, F);
    mont_mul<C>(Y3, E, t1, F);
    mont_add<C>(eight, Cc, Cc, F);
    mont_add<C>(eight, eight, eight, F);
    mont_add<C>(eight, eight, eight, F);
    mont_sub<C>(Y3, Y3, eight, F);
    fe<C> Z3;
    mont_mul<C>(Z3, q.Y, q.Z, F);
    mont_add<C>(Z3, Z3, Z3, F);
    r = Jac<C>{X3, Y3, Z3, is_zero<C>(Z3)};
}

/**
 * Point addition, branch-free. It always computes the general add formula, then constant-time-
 * selects the correct result over the special cases via masks: a==inf -> b, b==inf -> a,
 * a==b -> double(a), a==-b -> infinity. Precedence is enforced by cmov ORDER (a==inf last / highest).
 * @tparam C the curve traits.
 * @param r receives a + b.
 * @param a first point.
 * @param b second point.
 * @complexity O(1) — the same fixed field-operation count for every input (the double is always computed).
 * @alloc none.
 * @test CheatahP256.ConstantTimePointOpsMatchReference
 */
template <WeierstrassCurve C>
void jac_add_ct(Jac<C>& r, const Jac<C>& a, const Jac<C>& b) {
    const Mont<C>& F = Fp<C>();
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
    r = Jac<C>{X3, Y3, Z3, false};  // start = the general-case result

    const u64 ma = ct_mask(is_zero<C>(a.Z));   // a is infinity
    const u64 mb = ct_mask(is_zero<C>(b.Z));   // b is infinity
    const u64 hz = ct_mask(is_zero<C>(H));
    const u64 rz = ct_mask(is_zero<C>(Rr));
    Jac<C> dbl{};
    jac_double_ct<C>(dbl, a);
    const Jac<C> infp = jac_infinity<C>();
    jac_cmov<C>(r, dbl, hz & rz);    // a == b        -> 2a
    jac_cmov<C>(r, infp, hz & ~rz);  // a == -b       -> infinity
    jac_cmov<C>(r, a, mb);           // b == infinity -> a
    jac_cmov<C>(r, b, ma);           // a == infinity -> b   (highest precedence, applied last)
    r.inf = is_zero<C>(r.Z);
}

/**
 * Constant-time table lookup: scan every entry, copying the one whose index == sel via a mask, so
 * the memory-access pattern (and timing) is independent of the secret selector.
 * @tparam C the curve traits.
 * @tparam N the table size.
 * @param out receives tbl[sel].
 * @param tbl the table.
 * @param sel the (secret) index.
 * @complexity O(N) — every entry is scanned by design.
 * @alloc none.
 * @test CheatahP256.SignKnownVector
 */
template <WeierstrassCurve C, std::size_t N>
void ct_select(Jac<C>& out, const std::array<Jac<C>, N>& tbl, unsigned sel) {
    out = jac_infinity<C>();
    for (unsigned i = 0; i < N; ++i) jac_cmov<C>(out, tbl[i], ct_mask(i == sel));
}

/**
 * k*G for a SECRET scalar k, in constant time: 64 doublings + 64 unconditional adds over the
 * fixed-base comb table. The old form skipped the add when the window was zero and indexed the
 * table by the secret selector — both leaked bits of k. Here every step does the same work
 * (branch-free double, masked table select, unconditional branch-free add — add of the T[0]=infinity
 * entry when the window is zero is a no-op via the CT add's masks). The field arithmetic beneath is
 * branch-free as well: every modular reduction is computed and then selected with a mask, so no
 * step's timing depends on the values flowing through it.
 * @tparam C the curve traits.
 * @param r receives k*G.
 * @param k the secret scalar.
 * @complexity O(1) — exactly 64 CT doublings, 64 CT table scans, and 64 CT adds.
 * @alloc none.
 * @test CheatahP256.SignKnownVector
 */
template <WeierstrassCurve C>
void jac_mul_base(Jac<C>& r, const fe<C>& k) {
    const auto& T = g_comb<C>();
    Jac<C> acc = jac_infinity<C>();
    for (int j = 63; j >= 0; --j) {
        Jac<C> t{};
        jac_double_ct<C>(t, acc);
        acc = t;
        unsigned sel = 0;
        for (std::size_t i = 0; i < C::kLimbs; ++i) sel |= static_cast<unsigned>((k[i] >> j) & 1u) << i;
        Jac<C> add{};
        ct_select<C>(add, T, sel);
        jac_add_ct<C>(t, acc, add);
        acc = t;
    }
    r = acc;
}

/**
 * Differentially validate the branch-free FIELD arithmetic against an independent reference.
 *
 * mont_add, mont_sub and mont_mul each end in a conditional reduction that is now computed
 * unconditionally and selected with a mask. @ref ct_add_selfcheck cannot police that: both sides
 * of its comparison call the same field ops, so an error there cancels. This checks the reductions
 * directly — the boundaries (0, 1, m-1) and a deterministic sweep whose intermediate sums and
 * differences land in `[m, 2m)`, the band the RFC 6979 vector never reaches — against a reference
 * written the obvious way.
 * @tparam C the curve traits.
 * @return true iff every case agrees.
 * @complexity O(1) — a fixed number of fixed-width operations.
 * @alloc none.
 * @test CheatahP256.ConstantTimeFieldOpsMatchReference
 */
template <WeierstrassCurve C>
bool ct_field_selfcheck() {
    const Mont<C>& F = Fp<C>();
    // Reference add/sub: the same mathematics, written with branches. Correctness here is easy to
    // see by eye, which is the point — it is the oracle, not the fast path.
    auto ref_add = [&](const fe<C>& a, const fe<C>& b) {
        fe<C> s{};
        const u64 c = add_carry<C>(s, a, b);
        fe<C> t{};
        const u64 br = sub_borrow<C>(t, s, F.m);
        if (c != 0 || br == 0) return t;   // overflowed, or already >= m
        return s;
    };
    auto ref_sub = [&](const fe<C>& a, const fe<C>& b) {
        fe<C> d{};
        const u64 br = sub_borrow<C>(d, a, b);
        if (br != 0) {
            fe<C> t{};
            add_carry<C>(t, d, F.m);
            return t;
        }
        return d;
    };

    fe<C> zero{};
    fe<C> one{};
    one[0] = 1;
    fe<C> mm1{};                            // m - 1: the top of the field, where a sum must reduce
    sub_borrow<C>(mm1, F.m, one);

    std::array<fe<C>, 5> seeds{zero, one, mm1, F.m, F.rr};
    for (const fe<C>& a : seeds) {
        for (const fe<C>& b : seeds) {
            fe<C> got{};
            mont_add<C>(got, a, b, F);
            if (got != ref_add(a, b)) return false;
            mont_sub<C>(got, a, b, F);
            if (got != ref_sub(a, b)) return false;
        }
    }

    // A deterministic sweep (a 64-bit xorshift, so the case list is identical on every run and on
    // every machine) driving operands across the whole range rather than the few the vectors hit.
    u64 st = 0x9E3779B97F4A7C15ULL;
    auto next = [&]() {
        st ^= st << 13;
        st ^= st >> 7;
        st ^= st << 17;
        return st;
    };
    for (int iter = 0; iter < 512; ++iter) {
        fe<C> a{};
        fe<C> b{};
        for (std::size_t i = 0; i < C::kLimbs; ++i) {
            a[i] = next();
            b[i] = next();
        }
        // Bring both into the field first, so the operands are the shape the real path sees.
        fe<C> ar{};
        fe<C> br2{};
        mont_mul<C>(ar, a, F.one, F);
        mont_mul<C>(br2, b, F.one, F);
        fe<C> got{};
        mont_add<C>(got, ar, br2, F);
        if (got != ref_add(ar, br2)) return false;
        mont_sub<C>(got, ar, br2, F);
        if (got != ref_sub(ar, br2)) return false;
        // A product must land in [0, m): the reduction is what this is really testing.
        fe<C> prod{};
        mont_mul<C>(prod, ar, br2, F);
        if (geq<C>(prod, F.m)) return false;
    }
    return true;
}

/**
 * Differential self-check for the constant-time point ops. A TEMPLATE, instantiated ONLY by the
 * p256/p384 test seam (so there is no such code in a production build), it confirms jac_add_ct /
 * jac_double_ct agree with the branchy reference jac_add / jac_double on the general case AND every
 * special case — a==b, a==-b, and infinity operands — which the signing path exercises rarely or
 * never, so this both proves correctness and drives those branches for coverage.
 * @tparam C the curve traits.
 * @return true iff every CT result matches the branchy reference.
 * @complexity O(1) — a fixed handful of point operations.
 * @alloc none.
 * @test CheatahP256.ConstantTimePointOpsMatchReference
 */
template <WeierstrassCurve C>
bool ct_add_selfcheck() {
    const Mont<C>& F = Fp<C>();
    auto affine_eq = [&](const Jac<C>& u, const Jac<C>& v) -> bool {
        const bool ui = is_zero<C>(u.Z), vi = is_zero<C>(v.Z);
        if (ui || vi) return ui == vi;  // both infinity, or neither
        auto affine = [&](const Jac<C>& p, fe<C>& x, fe<C>& y) {
            fe<C> zi, zi2, zi3, xm, ym;
            mont_inv<C>(zi, p.Z, F);
            mont_mul<C>(zi2, zi, zi, F);
            mont_mul<C>(zi3, zi2, zi, F);
            mont_mul<C>(xm, p.X, zi2, F);
            mont_mul<C>(ym, p.Y, zi3, F);
            from_mont<C>(x, xm, F);
            from_mont<C>(y, ym, F);
        };
        fe<C> ux, uy, vx, vy;
        affine(u, ux, uy);
        affine(v, vx, vy);
        return ux == vx && uy == vy;
    };
    // Reference points via the branchy ops: P = 3G, Q = 5G, and -P.
    const Jac<C>& G = base_point<C>();
    Jac<C> P{}, Q{}, tmp{};
    jac_double<C>(tmp, G);      // 2G
    jac_add<C>(P, tmp, G);      // 3G
    jac_double<C>(tmp, tmp);    // 4G
    jac_add<C>(Q, tmp, G);      // 5G
    fe<C> zero{};
    Jac<C> negP = P;
    mont_sub<C>(negP.Y, zero, P.Y, F);  // -P = (X, -Y, Z)
    const Jac<C> inf = jac_infinity<C>();

    Jac<C> ct{}, ref{};
    bool ok = true;
    jac_add_ct<C>(ct, P, Q);   jac_add<C>(ref, P, Q);   ok &= affine_eq(ct, ref);   // general
    jac_add_ct<C>(ct, P, P);   jac_double<C>(ref, P);   ok &= affine_eq(ct, ref);   // a == b
    jac_add_ct<C>(ct, P, negP);                          ok &= is_zero<C>(ct.Z);     // a == -b -> infinity
    jac_add_ct<C>(ct, inf, P);                           ok &= affine_eq(ct, P);     // a == infinity
    jac_add_ct<C>(ct, P, inf);                           ok &= affine_eq(ct, P);     // b == infinity
    jac_add_ct<C>(ct, inf, inf);                         ok &= is_zero<C>(ct.Z);     // inf + inf
    jac_double_ct<C>(ct, P);   jac_double<C>(ref, P);   ok &= affine_eq(ct, ref);   // double general
    jac_double_ct<C>(ct, inf);                           ok &= is_zero<C>(ct.Z);     // double infinity
    return ok;
}

/**
 * Reduce a scalar already known to be < 2n into [0, n): a single conditional
 * subtraction of the group order n. Used for the FIPS 186-4 hash truncation and
 * for folding a curve x-coordinate (which lives in [0, p) < 2n) into a scalar.
 * @tparam C the curve traits.
 * @param v the value, < 2n.
 * @return v mod n.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahP256.ReduceModNBoundary
 */
template <WeierstrassCurve C>
fe<C> reduce_mod_n(const fe<C>& v) {
    if (geq<C>(v, C::N)) {
        fe<C> t;
        sub_borrow<C>(t, v, C::N);
        return t;
    }
    return v;
}

/**
 * Reduce a big-endian hash to a scalar in [0, n). A hash of at least kBytes keeps its
 * leftmost kBytes (the FIPS 186-4 leftmost-bits truncation); a SHORTER hash is the whole
 * value (X9.62 bits2int — right-aligned), e.g. a SHA-256 signature under a P-384 key.
 * @tparam C the curve traits.
 * @param h the digest bytes.
 * @return the scalar in [0, n).
 * @complexity O(1) — at most kBytes are copied regardless of the hash length.
 * @alloc none — a stack buffer.
 * @test CheatahP256.HashToScalarReducesWhenGreaterThanOrder
 * @test CheatahP384.HashToScalarReducesWhenGreaterThanOrder
 */
template <WeierstrassCurve C>
fe<C> hash_to_scalar(const std::string& h) {
    unsigned char buf[kBytes<C>] = {0};
    if (h.size() >= kBytes<C>)
        std::memcpy(buf, h.data(), kBytes<C>);
    else
        std::memcpy(buf + (kBytes<C> - h.size()), h.data(), h.size());  // NOLINT(bugprone-not-null-terminated-result): raw big-endian bytes, not a C string
    return reduce_mod_n<C>(be_to_fe<C>(buf));
}

// ---- minimal DER helpers ------------------------------------------------------------------------
/**
 * Parse SEQUENCE{INTEGER r, INTEGER s} -> kBytes big-endian r and s.
 * Short-form lengths only: both curves' SEQUENCE stays under 128 bytes (P-384: <= ~104).
 * @tparam C the curve traits.
 * @param der the DER-encoded signature.
 * @param r receives kBytes big-endian r.
 * @param s receives kBytes big-endian s.
 * @return false on any malformed encoding.
 * @complexity O(1) — short-form DER caps the accepted input at 129 bytes (a longer @p der
 *   fails the exact-length check without being scanned).
 * @alloc none.
 * @test CheatahP256.VerifyDerWithLeadingZeroIntegers
 */
template <WeierstrassCurve C>
bool der_to_rs(const std::string& der, unsigned char* r, unsigned char* s) {
    const auto* p = reinterpret_cast<const unsigned char*>(der.data());
    std::size_t n = der.size(), i = 0;
    auto read_int = [&](unsigned char* out) -> bool {
        if (i >= n) return false;
        const unsigned char int_tag = p[i++];
        if (int_tag != 0x02) return false;
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
    if (i >= n) return false;
    const unsigned char seq_tag = p[i++];
    if (seq_tag != 0x30) return false;
    if (i >= n) return false;
    std::size_t seqlen = p[i++];
    if (seqlen & 0x80) return false;
    if (i + seqlen != n) return false;
    return read_int(r) && read_int(s);
}

/**
 * Is (x, y) on the curve y^2 = x^3 - 3x + b (mod p)? Rejects an off-curve /
 * invalid-curve public key — SP 800-56A / FIPS 186 point validation, which the plain
 * coordinate-range check (x,y < p) does not catch.
 * @tparam C the curve traits.
 * @param x the affine x-coordinate (plain form, < p).
 * @param y the affine y-coordinate (plain form, < p).
 * @return true iff the point satisfies the curve equation.
 * @complexity O(1) — a fixed handful of field operations.
 * @alloc none.
 * @test CheatahP256.RejectsOffCurvePublicKey
 * @test CheatahP384.RejectsOffCurvePublicKey
 */
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

/**
 * ECDSA verification over raw byte forms: pubkey = 2*kBytes X||Y, sig = 2*kBytes r||s.
 * @tparam C the curve traits.
 * @param pubkey_xy the public key point, 2*kBytes X||Y big-endian.
 * @param msg_hash the message digest (truncated/reduced by hash_to_scalar).
 * @param sig_raw the signature, 2*kBytes r||s big-endian.
 * @return true iff the signature verifies (range checks, on-curve check, and x == r all pass).
 * @complexity O(1) — two scalar multiplications, computed as one Strauss-Shamir double chain.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 * @test CheatahP384.VerifyKnownVector
 */
template <WeierstrassCurve C>
bool verify_raw(const std::string& pubkey_xy, const std::string& msg_hash,
                const std::string& sig_raw) {
    if (pubkey_xy.size() != 2 * kBytes<C> || sig_raw.size() != 2 * kBytes<C>) return false;
    fe<C> r = be_to_fe<C>(reinterpret_cast<const unsigned char*>(sig_raw.data()));
    fe<C> s = be_to_fe<C>(reinterpret_cast<const unsigned char*>(sig_raw.data()) + kBytes<C>);
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

    fe<C> qx = be_to_fe<C>(reinterpret_cast<const unsigned char*>(pubkey_xy.data()));
    fe<C> qy = be_to_fe<C>(reinterpret_cast<const unsigned char*>(pubkey_xy.data()) + kBytes<C>);
    if (geq<C>(qx, C::P) || geq<C>(qy, C::P)) return false;
    if (!on_curve<C>(qx, qy)) return false;  // reject off-curve / invalid-curve public keys
    Jac<C> Q = affine_to_jac<C>(qx, qy);

    Jac<C> R{};
    jac_double_mul<C>(R, u1, base_point<C>(), u2, Q);  // u1*G + u2*Q, one doubling chain
    if (R.inf || is_zero<C>(R.Z)) return false;
    fe<C> x = reduce_mod_n<C>(jac_affine_x<C>(R));
    return std::memcmp(x.data(), r.data(), sizeof(fe<C>)) == 0;
}

/**
 * ECDSA verification of the DER form (SEQUENCE{INTEGER r, INTEGER s} — TLS/X.509).
 * @tparam C the curve traits.
 * @param pubkey_xy the public key point, 2*kBytes X||Y big-endian.
 * @param msg_hash the message digest.
 * @param sig_der the DER-encoded signature.
 * @return true iff the DER parses and the signature verifies.
 * @complexity O(1) — der_to_rs plus one verify_raw.
 * @alloc a temporary raw r||s signature string.
 * @test CheatahP256.VerifyDerWithLeadingZeroIntegers
 * @test CheatahP384.VerifyDerWithLeadingZeroIntegers
 */
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

/**
 * Encode a raw r||s signature (2*kBytes big-endian bytes) as the DER
 * `SEQUENCE{INTEGER r, INTEGER s}` that TLS CertificateVerify and X.509 carry — the
 * exact inverse of @ref der_to_rs. Integers are minimal-form: leading zero bytes are
 * stripped and a 0x00 sign byte is prepended when the top bit is set, so the output
 * round-trips through any strict DER parser. The outer length always fits short form
 * (max 2*(kBytes+3) = 102 bytes at P-384).
 * @tparam C the curve traits.
 * @param sig_raw the 2*kBytes r||s signature (e.g. sign_raw's output).
 * @return the DER bytes, or "" if @p sig_raw has the wrong length or a zero integer
 *         (r = 0 / s = 0 is never a valid ECDSA signature).
 * @complexity O(kBytes).
 * @alloc the returned string plus the two integer temporaries.
 * @test CheatahP256.RsToDerRoundTripsAndRejects
 */
template <WeierstrassCurve C>
std::string rs_to_der(const std::string& sig_raw) {
    if (sig_raw.size() != 2 * kBytes<C>) return "";
    const auto encode_int = [](const unsigned char* v) -> std::string {
        std::size_t i = 0;
        while (i < kBytes<C> - 1 && v[i] == 0) ++i;  // strip leading zeros, keep >= 1 byte
        if (v[i] == 0) return "";                    // the integer is zero — not a signature
        const bool sign = (v[i] & 0x80) != 0;
        std::string out;
        out.push_back(0x02);
        out.push_back(static_cast<char>((kBytes<C> - i) + (sign ? 1 : 0)));
        if (sign) out.push_back('\0');
        out.append(reinterpret_cast<const char*>(v + i), kBytes<C> - i);
        return out;
    };
    const std::string r = encode_int(reinterpret_cast<const unsigned char*>(sig_raw.data()));
    const std::string s = encode_int(reinterpret_cast<const unsigned char*>(sig_raw.data()) + kBytes<C>);
    if (r.empty() || s.empty()) return "";
    std::string der;
    der.push_back(0x30);
    der.push_back(static_cast<char>(r.size() + s.size()));
    return der + r + s;
}

}  // namespace cheatah::ec
