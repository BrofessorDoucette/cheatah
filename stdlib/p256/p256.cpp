// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// p256.cpp — NIST P-256 (secp256r1) ECDSA, from scratch. See p256.hpp.
//
// Representation: a 256-bit value is uint64_t[4], LEAST-significant limb first.
// Arithmetic mod the field prime p and mod the group order n is done in
// Montgomery form (R = 2^256); the Montgomery constants are derived from the
// modulus at startup, so there are no hand-transcribed magic numbers to get
// wrong. Points are Jacobian (X:Y:Z) with the curve a = -3.

#include "p256.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include "hashlib.hpp"  // hmac_sha256 for the RFC 6979 deterministic nonce

namespace cheatah::p256 {

namespace {

using u64 = std::uint64_t;
using u128 = unsigned __int128;
using fe = std::array<u64, 4>;  // a 256-bit value, limb[0] = least significant

// ---- P-256 constants (normal form, little-endian limbs) --------------------
constexpr fe P = {0xFFFFFFFFFFFFFFFFull, 0x00000000FFFFFFFFull, 0x0000000000000000ull,
                  0xFFFFFFFF00000001ull};
constexpr fe N = {0xF3B9CAC2FC632551ull, 0xBCE6FAADA7179E84ull, 0xFFFFFFFFFFFFFFFFull,
                  0xFFFFFFFF00000000ull};
constexpr fe CURVE_B = {0x3BCE3C3E27D2604Bull, 0x651D06B0CC53B0F6ull, 0xB3EBBD55769886BCull,
                        0x5AC635D8AA3A93E7ull};
constexpr fe GX = {0xF4A13945D898C296ull, 0x77037D812DEB33A0ull, 0xF8BCE6E563A440F2ull,
                   0x6B17D1F2E12C4247ull};
constexpr fe GY = {0xCBB6406837BF51F5ull, 0x2BCE33576B315ECEull, 0x8EE7EB4A7C0F9E16ull,
                   0x4FE342E2FE1A7F9Bull};

// ---- plain 256-bit helpers -------------------------------------------------
bool is_zero(const fe& a) { return (a[0] | a[1] | a[2] | a[3]) == 0; }
bool geq(const fe& a, const fe& b) {  // a >= b
    for (int i = 3; i >= 0; --i)
        if (a[i] != b[i]) return a[i] > b[i];
    return true;
}
// a - b (mod 2^256), returns borrow.
u64 sub_borrow(fe& r, const fe& a, const fe& b) {
    u128 br = 0;
    for (int i = 0; i < 4; ++i) {
        u128 d = (u128)a[i] - b[i] - br;
        r[i] = (u64)d;
        br = (d >> 64) & 1;
    }
    return (u64)br;
}
// a + b (mod 2^256), returns carry.
u64 add_carry(fe& r, const fe& a, const fe& b) {
    u128 c = 0;
    for (int i = 0; i < 4; ++i) {
        u128 s = (u128)a[i] + b[i] + c;
        r[i] = (u64)s;
        c = s >> 64;
    }
    return (u64)c;
}

// ---- Montgomery context for one modulus ------------------------------------
struct Mont {
    fe m;       // the modulus
    fe rr;      // R^2 mod m  (R = 2^256)
    fe one;     // R mod m    (Montgomery form of 1)
    u64 n0;     // -m^{-1} mod 2^64
};

// CIOS Montgomery multiplication: r = a*b*R^-1 mod m.
void mont_mul(fe& r, const fe& a, const fe& b, const Mont& M) {
    u64 t[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        // t += a * b[i]
        u128 carry = 0;
        for (int j = 0; j < 4; ++j) {
            u128 p = (u128)a[j] * b[i] + t[j] + carry;
            t[j] = (u64)p;
            carry = p >> 64;
        }
        u128 s = (u128)t[4] + carry;
        t[4] = (u64)s;
        u64 top = (u64)(s >> 64);
        // m_mul = t[0] * n0 mod 2^64; t += m_mul * m; then shift right one limb
        u64 mmul = (u64)((u128)t[0] * M.n0);
        carry = 0;
        {
            u128 p = (u128)mmul * M.m[0] + t[0];
            carry = p >> 64;  // low limb becomes 0
        }
        for (int j = 1; j < 4; ++j) {
            u128 p = (u128)mmul * M.m[j] + t[j] + carry;
            t[j - 1] = (u64)p;
            carry = p >> 64;
        }
        u128 s2 = (u128)t[4] + carry;
        t[3] = (u64)s2;
        t[4] = top + (u64)(s2 >> 64);
    }
    fe res = {t[0], t[1], t[2], t[3]};
    // final conditional subtraction (t may be in [0, 2m))
    if (t[4] != 0 || geq(res, M.m)) {
        fe tmp;
        sub_borrow(tmp, res, M.m);
        res = tmp;
    }
    r = res;
}
void mont_add(fe& r, const fe& a, const fe& b, const Mont& M) {
    fe s;
    u64 c = add_carry(s, a, b);
    if (c || geq(s, M.m)) {
        fe t;
        sub_borrow(t, s, M.m);
        s = t;
    }
    r = s;
}
void mont_sub(fe& r, const fe& a, const fe& b, const Mont& M) {
    fe d;
    u64 br = sub_borrow(d, a, b);
    if (br) {
        fe t;
        add_carry(t, d, M.m);
        d = t;
    }
    r = d;
}
void to_mont(fe& r, const fe& a, const Mont& M) { mont_mul(r, a, M.rr, M); }
void from_mont(fe& r, const fe& a, const Mont& M) {
    fe one = {1, 0, 0, 0};
    mont_mul(r, a, one, M);
}
// r = a^-1 mod m, via Fermat: a^(m-2). (m is prime for both p and n.)
void mont_inv(fe& r, const fe& a, const Mont& M) {
    fe exp;
    sub_borrow(exp, M.m, fe{2, 0, 0, 0});  // m - 2
    fe result = M.one;                     // Montgomery 1
    fe base = a;
    for (int i = 0; i < 256; ++i) {
        if ((exp[i / 64] >> (i % 64)) & 1) mont_mul(result, result, base, M);
        mont_mul(base, base, base, M);
    }
    r = result;
}

u64 inv64(u64 a) {  // a^-1 mod 2^64 (a odd), Newton's iteration
    u64 x = a;      // correct to 3 bits
    for (int i = 0; i < 5; ++i) x *= 2 - a * x;
    return x;
}
Mont make_mont(const fe& m) {
    Mont M;
    M.m = m;
    M.n0 = 0 - inv64(m[0]);
    // rr = 2^512 mod m, by 512 doublings of 1 with conditional subtract.
    fe x = {1, 0, 0, 0};
    for (int i = 0; i < 512; ++i) {
        fe d;
        u64 c = add_carry(d, x, x);
        if (c || geq(d, m)) {
            fe t;
            sub_borrow(t, d, m);
            d = t;
        }
        x = d;
    }
    M.rr = x;
    // one = R mod m = 2^256 mod m  -> to_mont(1)
    fe oneN = {1, 0, 0, 0};
    mont_mul(M.one, oneN, M.rr, M);
    return M;
}

// ---- the two field contexts (built once) -----------------------------------
const Mont& Fp() {
    static const Mont m = make_mont(P);
    return m;
}
const Mont& Fn() {
    static const Mont m = make_mont(N);
    return m;
}

// ---- bytes <-> limbs (32 big-endian bytes) ---------------------------------
fe be_to_fe(const unsigned char* b) {
    fe r{};
    for (int limb = 0; limb < 4; ++limb) {
        u64 v = 0;
        const unsigned char* p = b + (3 - limb) * 8;  // most-significant 8 bytes -> limb[3]
        for (int k = 0; k < 8; ++k) v = (v << 8) | p[k];
        r[limb] = v;
    }
    return r;
}
void fe_to_be(unsigned char* out, const fe& a) {
    for (int limb = 0; limb < 4; ++limb) {
        u64 v = a[limb];
        unsigned char* p = out + (3 - limb) * 8;
        for (int k = 7; k >= 0; --k) {
            p[k] = (unsigned char)(v & 0xFF);
            v >>= 8;
        }
    }
}

// ---- Jacobian points (coordinates in Montgomery form, mod p) ---------------
struct Jac {
    fe X, Y, Z;
    bool inf;
};
Jac jac_infinity() { return Jac{Fp().one, Fp().one, {0, 0, 0, 0}, true}; }

void jac_double(Jac& r, const Jac& q) {
    const Mont& F = Fp();
    if (q.inf || is_zero(q.Z)) {
        r = jac_infinity();
        return;
    }
    fe A, B, C, D, t1, t2;
    mont_mul(A, q.X, q.X, F);  // X^2
    mont_mul(B, q.Y, q.Y, F);  // Y^2
    mont_mul(C, B, B, F);      // Y^4
    // D = 2*((X+B)^2 - A - C)
    mont_add(t1, q.X, B, F);
    mont_mul(t1, t1, t1, F);
    mont_sub(t1, t1, A, F);
    mont_sub(t1, t1, C, F);
    mont_add(D, t1, t1, F);
    // ZZ = Z^2 ; E = 3*(X - ZZ)*(X + ZZ)  [uses a = -3]
    fe ZZ;
    mont_mul(ZZ, q.Z, q.Z, F);
    mont_sub(t1, q.X, ZZ, F);
    mont_add(t2, q.X, ZZ, F);
    mont_mul(t1, t1, t2, F);
    fe E;
    mont_add(E, t1, t1, F);
    mont_add(E, E, t1, F);  // 3*(...)
    // F2 = E^2 ; X3 = F2 - 2D
    fe X3;
    mont_mul(X3, E, E, F);
    mont_sub(X3, X3, D, F);
    mont_sub(X3, X3, D, F);
    // Y3 = E*(D - X3) - 8C
    fe Y3, eight;
    mont_sub(t1, D, X3, F);
    mont_mul(Y3, E, t1, F);
    mont_add(eight, C, C, F);
    mont_add(eight, eight, eight, F);
    mont_add(eight, eight, eight, F);  // 8C
    mont_sub(Y3, Y3, eight, F);
    // Z3 = 2*Y*Z
    fe Z3;
    mont_mul(Z3, q.Y, q.Z, F);
    mont_add(Z3, Z3, Z3, F);
    r = Jac{X3, Y3, Z3, false};
}

void jac_add(Jac& r, const Jac& a, const Jac& b) {
    const Mont& F = Fp();
    if (a.inf || is_zero(a.Z)) {
        r = b;
        return;
    }
    if (b.inf || is_zero(b.Z)) {
        r = a;
        return;
    }
    fe Z1Z1, Z2Z2, U1, U2, S1, S2;
    mont_mul(Z1Z1, a.Z, a.Z, F);
    mont_mul(Z2Z2, b.Z, b.Z, F);
    mont_mul(U1, a.X, Z2Z2, F);
    mont_mul(U2, b.X, Z1Z1, F);
    fe t;
    mont_mul(t, b.Z, Z2Z2, F);
    mont_mul(S1, a.Y, t, F);
    mont_mul(t, a.Z, Z1Z1, F);
    mont_mul(S2, b.Y, t, F);
    fe H, Rr;
    mont_sub(H, U2, U1, F);
    mont_sub(Rr, S2, S1, F);
    if (is_zero(H)) {
        if (is_zero(Rr)) {
            jac_double(r, a);
            return;
        }
        r = jac_infinity();
        return;
    }
    fe HH, HHH, V;
    mont_mul(HH, H, H, F);
    mont_mul(HHH, HH, H, F);
    mont_mul(V, U1, HH, F);
    fe X3;
    mont_mul(X3, Rr, Rr, F);
    mont_sub(X3, X3, HHH, F);
    mont_sub(X3, X3, V, F);
    mont_sub(X3, X3, V, F);
    fe Y3;
    mont_sub(t, V, X3, F);
    mont_mul(Y3, Rr, t, F);
    fe s1hhh;
    mont_mul(s1hhh, S1, HHH, F);
    mont_sub(Y3, Y3, s1hhh, F);
    fe Z3;
    mont_mul(Z3, a.Z, b.Z, F);
    mont_mul(Z3, Z3, H, F);
    r = Jac{X3, Y3, Z3, false};
}

// Strauss-Shamir: u1*A + u2*B with ONE doubling chain (256 doublings total)
// instead of two separate scalar multiplications (512). A 2-bit window over both
// scalars uses a 16-entry combined table [i*A + j*B] so it also halves the adds.
void jac_double_mul(Jac& r, const fe& u1, const Jac& A, const fe& u2, const Jac& B) {
    Jac tbl[4][4];  // tbl[i][j] = i*A + j*B, i,j in {0..3}
    tbl[0][0] = jac_infinity();
    tbl[1][0] = A;
    jac_double(tbl[2][0], A);
    jac_add(tbl[3][0], tbl[2][0], A);
    tbl[0][1] = B;
    jac_double(tbl[0][2], B);
    jac_add(tbl[0][3], tbl[0][2], B);
    for (int i = 1; i < 4; ++i)
        for (int j = 1; j < 4; ++j) jac_add(tbl[i][j], tbl[i][0], tbl[0][j]);

    Jac acc = jac_infinity();
    for (int i = 254; i >= 0; i -= 2) {
        Jac t;
        jac_double(t, acc);
        acc = t;
        jac_double(t, acc);
        acc = t;
        const unsigned a = (u1[i / 64] >> (i % 64)) & 0x3;
        const unsigned b = (u2[i / 64] >> (i % 64)) & 0x3;
        if (a || b) {
            jac_add(t, acc, tbl[a][b]);
            acc = t;
        }
    }
    r = acc;
}

// affine x-coordinate (normal form) of a Jacobian point: x = X / Z^2.
fe jac_affine_x(const Jac& q) {
    const Mont& F = Fp();
    fe zinv, zinv2, x;
    mont_inv(zinv, q.Z, F);
    mont_mul(zinv2, zinv, zinv, F);
    mont_mul(x, q.X, zinv2, F);
    fe out;
    from_mont(out, x, F);
    return out;
}

Jac affine_to_jac(const fe& x, const fe& y) {
    const Mont& F = Fp();
    Jac p;
    to_mont(p.X, x, F);
    to_mont(p.Y, y, F);
    p.Z = F.one;
    p.inf = false;
    return p;
}
const Jac& base_point() {
    static const Jac g = affine_to_jac(GX, GY);
    return g;
}

// Fixed-base comb for k*G. G is constant, so we precompute (once) the 16-entry
// table T[s] = sum over set bits i of s of (2^(64*i) * G). Then k*G is just 64
// doublings + 64 adds (vs 256 doublings for a generic window) — the big win for
// the per-message signing path. Selector at step j is bit j of each of the four
// 64-bit limbs.
const std::array<Jac, 16>& g_comb() {
    static const std::array<Jac, 16> tbl = [] {
        Jac gi[4];
        gi[0] = affine_to_jac(GX, GY);
        for (int i = 1; i < 4; ++i) {
            Jac acc = gi[i - 1];
            for (int b = 0; b < 64; ++b) {  // gi[i] = 2^64 * gi[i-1]
                Jac t;
                jac_double(t, acc);
                acc = t;
            }
            gi[i] = acc;
        }
        std::array<Jac, 16> t;
        t[0] = jac_infinity();
        for (unsigned s = 1; s < 16; ++s) {
            Jac acc = jac_infinity();
            for (int i = 0; i < 4; ++i)
                if (s & (1u << i)) {
                    Jac r;
                    jac_add(r, acc, gi[i]);
                    acc = r;
                }
            t[s] = acc;
        }
        return t;
    }();
    return tbl;
}

void jac_mul_base(Jac& r, const fe& k) {
    const std::array<Jac, 16>& T = g_comb();
    Jac acc = jac_infinity();
    for (int j = 63; j >= 0; --j) {
        Jac t;
        jac_double(t, acc);
        acc = t;
        const unsigned sel = ((k[0] >> j) & 1u) | (((k[1] >> j) & 1u) << 1) |
                             (((k[2] >> j) & 1u) << 2) | (((k[3] >> j) & 1u) << 3);
        if (sel) {
            jac_add(t, acc, T[sel]);
            acc = t;
        }
    }
    r = acc;
}

// Reduce a scalar already known to be < 2n into [0, n): a single conditional
// subtraction of the group order n. Used for the FIPS 186-4 hash truncation and
// for folding a curve x-coordinate (which lives in [0, p) < 2n) into a scalar.
fe reduce_mod_n(const fe& v) {
    if (geq(v, N)) {
        fe t;
        sub_borrow(t, v, N);
        return t;
    }
    return v;
}

// reduce a 32-byte big-endian hash to a scalar in [0, n) (FIPS 186-4 leftmost bits).
fe hash_to_scalar(const std::string& h) {
    unsigned char buf[32] = {0};
    const std::size_t take = h.size() < 32 ? h.size() : 32;
    std::memcpy(buf, h.data(), take);  // leftmost 32 bytes
    return reduce_mod_n(be_to_fe(buf));
}

// ---- minimal DER helpers ---------------------------------------------------
// Parse SEQUENCE{INTEGER r, INTEGER s} -> 32-byte big-endian r and s. false on error.
bool der_to_rs(const std::string& der, unsigned char r[32], unsigned char s[32]) {
    const unsigned char* p = (const unsigned char*)der.data();
    std::size_t n = der.size(), i = 0;
    auto read_int = [&](unsigned char out[32]) -> bool {
        if (i >= n || p[i++] != 0x02) return false;
        if (i >= n) return false;
        std::size_t len = p[i++];
        if (len & 0x80) return false;  // P-256 ints are short-form
        if (i + len > n || len == 0) return false;
        const unsigned char* v = p + i;
        // strip a leading zero (sign byte)
        while (len > 1 && v[0] == 0) {
            ++v;
            --len;
        }
        if (len > 32) return false;
        std::memset(out, 0, 32);
        std::memcpy(out + (32 - len), v, len);
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

}  // namespace

#ifdef CHEATAH_P256_TESTING
namespace testonly {
// Test seam: reduce a 32-byte big-endian scalar mod n via the SAME reduce_mod_n
// used by verify/sign. The one-conditional-subtraction path (value in [n, p)) is a
// ~2^-128-measure event on real curve x-coordinates, so it is not reachable through
// verify_raw/sign_raw with real inputs; this drives it directly on the real code.
std::string reduce_mod_n_be(const std::string& be32) {
    fe v = be_to_fe((const unsigned char*)be32.data());
    fe r = reduce_mod_n(v);
    std::string out(32, '\0');
    fe_to_be((unsigned char*)out.data(), r);
    return out;
}
}  // namespace testonly
#endif

// Is (x, y) on the P-256 curve y^2 = x^3 - 3x + b (mod p)? Rejects an off-curve /
// invalid-curve public key — SP 800-56A / FIPS 186 point validation, which the plain
// coordinate-range check (x,y < p) does not catch.
bool on_curve(const fe& x, const fe& y) {
    const Mont& F = Fp();
    fe xm, ym, x2, x3, tx, rhs, bm, y2;
    to_mont(xm, x, F);
    to_mont(ym, y, F);
    mont_mul(x2, xm, xm, F);    // x^2
    mont_mul(x3, x2, xm, F);    // x^3
    mont_add(tx, xm, xm, F);    // 2x
    mont_add(tx, tx, xm, F);    // 3x
    mont_sub(rhs, x3, tx, F);   // x^3 - 3x
    to_mont(bm, CURVE_B, F);
    mont_add(rhs, rhs, bm, F);  // x^3 - 3x + b
    mont_mul(y2, ym, ym, F);    // y^2
    return std::memcmp(y2.data(), rhs.data(), sizeof(fe)) == 0;
}

bool verify_raw(const std::string& pubkey_xy, const std::string& msg_hash,
                const std::string& sig_raw) {
    if (pubkey_xy.size() != 64 || sig_raw.size() != 64) return false;
    fe r = be_to_fe((const unsigned char*)sig_raw.data());
    fe s = be_to_fe((const unsigned char*)sig_raw.data() + 32);
    if (is_zero(r) || is_zero(s) || geq(r, N) || geq(s, N)) return false;

    const Mont& Fnn = Fn();
    fe e = hash_to_scalar(msg_hash);
    fe sm, em, rm, w, u1, u2;
    to_mont(sm, s, Fnn);
    mont_inv(w, sm, Fnn);  // w = s^-1 (Montgomery)
    to_mont(em, e, Fnn);
    to_mont(rm, r, Fnn);
    fe u1m, u2m;
    mont_mul(u1m, em, w, Fnn);
    mont_mul(u2m, rm, w, Fnn);
    from_mont(u1, u1m, Fnn);
    from_mont(u2, u2m, Fnn);

    fe qx = be_to_fe((const unsigned char*)pubkey_xy.data());
    fe qy = be_to_fe((const unsigned char*)pubkey_xy.data() + 32);
    if (geq(qx, P) || geq(qy, P)) return false;
    if (!on_curve(qx, qy)) return false;  // reject off-curve / invalid-curve public keys
    Jac Q = affine_to_jac(qx, qy);

    Jac R;
    jac_double_mul(R, u1, base_point(), u2, Q);  // u1*G + u2*Q, one doubling chain
    if (R.inf || is_zero(R.Z)) return false;
    fe x = reduce_mod_n(jac_affine_x(R));
    return std::memcmp(x.data(), r.data(), sizeof(fe)) == 0;
}

bool verify_der(const std::string& pubkey_xy, const std::string& msg_hash,
                const std::string& sig_der) {
    unsigned char r[32], s[32];
    if (!der_to_rs(sig_der, r, s)) return false;
    std::string raw;
    raw.resize(64);
    std::memcpy(raw.data(), r, 32);
    std::memcpy(raw.data() + 32, s, 32);
    return verify_raw(pubkey_xy, msg_hash, raw);
}

#ifdef CHEATAH_P256_TESTING
// In test builds the RFC 6979 retry tail — normally a ~2^-128 event on real inputs —
// is driven by rejecting the first `force_retries` otherwise-valid nonce candidates.
// This whole parameter and its use compile out of release builds.
std::string sign_raw_impl(const std::string& privkey, const std::string& msg_hash,
                          int force_retries) {
#else
std::string sign_raw(const std::string& privkey, const std::string& msg_hash) {
#endif
    if (privkey.size() != 32) return std::string();
    const Mont& Fnn = Fn();
    fe d = be_to_fe((const unsigned char*)privkey.data());
    if (is_zero(d) || geq(d, N)) return std::string();
    fe e = hash_to_scalar(msg_hash);

    // RFC 6979 deterministic nonce generation (HMAC-SHA256).
    unsigned char h1[32] = {0};
    std::memcpy(h1, msg_hash.data(), msg_hash.size() < 32 ? msg_hash.size() : 32);
    unsigned char x[32];
    fe_to_be(x, d);
    std::string V(32, '\x01'), K(32, '\x00');
    auto bin = [](const fe&) {};
    (void)bin;
    auto hmac = [](const std::string& key, const std::string& msg) {
        return hashlib::hmac_sha256(key, msg);  // raw 32-byte digest
    };
    // K = HMAC(K, V || 0x00 || int2octets(x) || bits2octets(h1))
    auto step = [&](unsigned char tag) {
        std::string in = V;
        if (tag != 0xFF) in.push_back((char)tag);
        if (tag != 0xFF) {
            in.append((const char*)x, 32);
            in.append((const char*)h1, 32);
        }
        K = hmac(K, in);
        V = hmac(K, V);
    };
    step(0x00);
    step(0x01);
    for (int attempt = 0; attempt < 64; ++attempt) {
        V = hmac(K, V);
        fe k = be_to_fe((const unsigned char*)V.data());
        if (!is_zero(k) && !geq(k, N)) {
            Jac R;
            jac_mul_base(R, k);  // fixed-base comb
            if (!(R.inf || is_zero(R.Z))) {
                fe rx = reduce_mod_n(jac_affine_x(R));
#ifdef CHEATAH_P256_TESTING
                if (force_retries > 0) {
                    --force_retries;  // reject this valid candidate; take the retry tail
                } else if (!is_zero(rx)) {
#else
                if (!is_zero(rx)) {
#endif
                    // s = k^-1 (e + r*d) mod n
                    fe km, kinv, rm, dm, em, rd, sum, sm;
                    to_mont(km, k, Fnn);
                    mont_inv(kinv, km, Fnn);
                    to_mont(rm, rx, Fnn);
                    to_mont(dm, d, Fnn);
                    to_mont(em, e, Fnn);
                    mont_mul(rd, rm, dm, Fnn);
                    mont_add(sum, em, rd, Fnn);
                    mont_mul(sm, kinv, sum, Fnn);
                    fe sfinal;
                    from_mont(sfinal, sm, Fnn);
                    if (!is_zero(sfinal)) {
                        std::string out(64, '\0');
                        fe_to_be((unsigned char*)out.data(), rx);
                        fe_to_be((unsigned char*)out.data() + 32, sfinal);
                        return out;
                    }
                }
            }
        }
        // K = HMAC(K, V || 0x00); V = HMAC(K, V)
        std::string in = V;
        in.push_back('\x00');
        K = hmac(K, in);
        V = hmac(K, V);
    }
    return std::string();
}

#ifdef CHEATAH_P256_TESTING
std::string sign_raw(const std::string& privkey, const std::string& msg_hash) {
    return sign_raw_impl(privkey, msg_hash, 0);
}

namespace testonly {
// Test seam: sign forcing `force_retries` RFC 6979 nonce rejections first, so the
// retry tail (and, at 64+, the exhausted "" return) runs on the real code path.
std::string sign_raw_skip(const std::string& privkey, const std::string& msg_hash,
                          int force_retries) {
    return sign_raw_impl(privkey, msg_hash, force_retries);
}
}  // namespace testonly
#endif

std::string public_from_private(const std::string& privkey) {
    if (privkey.size() != 32) return std::string();
    fe d = be_to_fe((const unsigned char*)privkey.data());
    if (is_zero(d) || geq(d, N)) return std::string();
    Jac Q;
    jac_mul_base(Q, d);  // d*G via the fixed-base comb
    if (Q.inf || is_zero(Q.Z)) return std::string();
    // affine x and y (normal form)
    const Mont& F = Fp();
    fe zinv, zinv2, zinv3, x, y;
    mont_inv(zinv, Q.Z, F);
    mont_mul(zinv2, zinv, zinv, F);
    mont_mul(zinv3, zinv2, zinv, F);
    mont_mul(x, Q.X, zinv2, F);
    mont_mul(y, Q.Y, zinv3, F);
    fe xo, yo;
    from_mont(xo, x, F);
    from_mont(yo, y, F);
    std::string out(64, '\0');
    fe_to_be((unsigned char*)out.data(), xo);
    fe_to_be((unsigned char*)out.data() + 32, yo);
    return out;
}

std::string spki_ec_point(std::string_view der) {
    // Find the uncompressed-point marker: BIT STRING (03) <len> 00 04 <X(32)><Y(32)>.
    // The OID id-ecPublicKey + prime256v1 precedes it; we anchor on the 0x04 point.
    const unsigned char* p = (const unsigned char*)der.data();
    const std::size_t n = der.size();
    for (std::size_t i = 0; i + 2 + 65 <= n; ++i) {
        // BIT STRING tag, then a length, then 00 (unused bits), then 04 (uncompressed)
        if (p[i] == 0x03 && p[i + 2] == 0x00 && p[i + 3] == 0x04) {
            const std::size_t len = p[i + 1];
            if (len == 66 && i + 4 + 64 <= n) return std::string((const char*)p + i + 4, 64);
        }
    }
    return std::string();
}

}  // namespace cheatah::p256
