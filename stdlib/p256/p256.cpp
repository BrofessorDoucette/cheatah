// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// p256.cpp — NIST P-256 (secp256r1) ECDSA, from scratch. See p256.hpp.
//
// The width-generic Weierstrass machinery (Montgomery field arithmetic, Jacobian group
// law, ECDSA verify — shared with the `p384` module) lives in ec_core.hpp; this file
// supplies the P-256 curve constants, the RFC 6979 deterministic signing path (P-256 +
// HMAC-SHA256 specific) and the SPKI point extraction. Representation: a 256-bit value
// is uint64_t[4], LEAST-significant limb first; the Montgomery constants are derived
// from the modulus at startup, so there are no hand-transcribed magic numbers to get
// wrong.

#include "p256.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include "ec_core.hpp"
#include "hashlib.hpp"  // hmac_sha256 for the RFC 6979 deterministic nonce

namespace cheatah::p256 {

namespace {

namespace ec = cheatah::ec;

// ---- P-256 curve traits (normal form, little-endian limbs) -----------------
struct P256Curve {
    static constexpr std::size_t kLimbs = 4;
    static constexpr std::array<ec::u64, 4> P = {0xFFFFFFFFFFFFFFFFull, 0x00000000FFFFFFFFull,
                                                 0x0000000000000000ull, 0xFFFFFFFF00000001ull};
    static constexpr std::array<ec::u64, 4> N = {0xF3B9CAC2FC632551ull, 0xBCE6FAADA7179E84ull,
                                                 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFF00000000ull};
    static constexpr std::array<ec::u64, 4> B = {0x3BCE3C3E27D2604Bull, 0x651D06B0CC53B0F6ull,
                                                 0xB3EBBD55769886BCull, 0x5AC635D8AA3A93E7ull};
    static constexpr std::array<ec::u64, 4> GX = {0xF4A13945D898C296ull, 0x77037D812DEB33A0ull,
                                                  0xF8BCE6E563A440F2ull, 0x6B17D1F2E12C4247ull};
    static constexpr std::array<ec::u64, 4> GY = {0xCBB6406837BF51F5ull, 0x2BCE33576B315ECEull,
                                                  0x8EE7EB4A7C0F9E16ull, 0x4FE342E2FE1A7F9Bull};
};
static_assert(ec::WeierstrassCurve<P256Curve>);

using fe = ec::fe<P256Curve>;
using Jac = ec::Jac<P256Curve>;

}  // namespace

#ifdef CHEATAH_P256_TESTING
namespace testonly {
// Test seam: reduce a 32-byte big-endian scalar mod n via the SAME reduce_mod_n
// used by verify/sign. The one-conditional-subtraction path (value in [n, p)) is a
// ~2^-128-measure event on real curve x-coordinates, so it is not reachable through
// verify_raw/sign_raw with real inputs; this drives it directly on the real code.
std::string reduce_mod_n_be(const std::string& be32) {
    fe v = ec::be_to_fe<P256Curve>((const unsigned char*)be32.data());
    fe r = ec::reduce_mod_n<P256Curve>(v);
    std::string out(32, '\0');
    ec::fe_to_be<P256Curve>((unsigned char*)out.data(), r);
    return out;
}
// Test seam: run the constant-time point-op differential self-check (jac_add_ct/jac_double_ct vs the
// branchy reference across general + a==b + a==-b + infinity cases). True iff the CT signing path's
// arithmetic matches the reference on every case. Drives the special-case branches the signing path
// doesn't reach with real nonces.
bool ct_point_selfcheck() { return ec::ct_add_selfcheck<P256Curve>(); }
}  // namespace testonly
#endif

bool verify_raw(const std::string& pubkey_xy, const std::string& msg_hash,
                const std::string& sig_raw) {
    return ec::verify_raw<P256Curve>(pubkey_xy, msg_hash, sig_raw);
}

bool verify_der(const std::string& pubkey_xy, const std::string& msg_hash,
                const std::string& sig_der) {
    return ec::verify_der<P256Curve>(pubkey_xy, msg_hash, sig_der);
}

std::string rs_to_der(const std::string& sig_raw) {
    return ec::rs_to_der<P256Curve>(sig_raw);
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
    const ec::Mont<P256Curve>& Fnn = ec::Fn<P256Curve>();
    fe d = ec::be_to_fe<P256Curve>((const unsigned char*)privkey.data());
    if (ec::is_zero<P256Curve>(d) || ec::geq<P256Curve>(d, P256Curve::N)) return std::string();
    fe e = ec::hash_to_scalar<P256Curve>(msg_hash);

    // RFC 6979 deterministic nonce generation (HMAC-SHA256).
    unsigned char h1[32] = {0};
    std::memcpy(h1, msg_hash.data(), msg_hash.size() < 32 ? msg_hash.size() : 32);
    unsigned char x[32];
    ec::fe_to_be<P256Curve>(x, d);
    std::string V(32, '\x01'), K(32, '\x00');
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
        fe k = ec::be_to_fe<P256Curve>((const unsigned char*)V.data());
        if (!ec::is_zero<P256Curve>(k) && !ec::geq<P256Curve>(k, P256Curve::N)) {
            Jac R;
            ec::jac_mul_base<P256Curve>(R, k);  // fixed-base comb
            if (!(R.inf || ec::is_zero<P256Curve>(R.Z))) {
                fe rx = ec::reduce_mod_n<P256Curve>(ec::jac_affine_x<P256Curve>(R));
#ifdef CHEATAH_P256_TESTING
                if (force_retries > 0) {
                    --force_retries;  // reject this valid candidate; take the retry tail
                } else if (!ec::is_zero<P256Curve>(rx)) {
#else
                if (!ec::is_zero<P256Curve>(rx)) {
#endif
                    // s = k^-1 (e + r*d) mod n
                    fe km, kinv, rm, dm, em, rd, sum, sm;
                    ec::to_mont<P256Curve>(km, k, Fnn);
                    ec::mont_inv<P256Curve>(kinv, km, Fnn);
                    ec::to_mont<P256Curve>(rm, rx, Fnn);
                    ec::to_mont<P256Curve>(dm, d, Fnn);
                    ec::to_mont<P256Curve>(em, e, Fnn);
                    ec::mont_mul<P256Curve>(rd, rm, dm, Fnn);
                    ec::mont_add<P256Curve>(sum, em, rd, Fnn);
                    ec::mont_mul<P256Curve>(sm, kinv, sum, Fnn);
                    fe sfinal;
                    ec::from_mont<P256Curve>(sfinal, sm, Fnn);
                    if (!ec::is_zero<P256Curve>(sfinal)) {
                        std::string out(64, '\0');
                        ec::fe_to_be<P256Curve>((unsigned char*)out.data(), rx);
                        ec::fe_to_be<P256Curve>((unsigned char*)out.data() + 32, sfinal);
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
    fe d = ec::be_to_fe<P256Curve>((const unsigned char*)privkey.data());
    if (ec::is_zero<P256Curve>(d) || ec::geq<P256Curve>(d, P256Curve::N)) return std::string();
    Jac Q;
    ec::jac_mul_base<P256Curve>(Q, d);  // d*G via the fixed-base comb
    if (Q.inf || ec::is_zero<P256Curve>(Q.Z)) return std::string();
    // affine x and y (normal form)
    const ec::Mont<P256Curve>& F = ec::Fp<P256Curve>();
    fe zinv, zinv2, zinv3, x, y;
    ec::mont_inv<P256Curve>(zinv, Q.Z, F);
    ec::mont_mul<P256Curve>(zinv2, zinv, zinv, F);
    ec::mont_mul<P256Curve>(zinv3, zinv2, zinv, F);
    ec::mont_mul<P256Curve>(x, Q.X, zinv2, F);
    ec::mont_mul<P256Curve>(y, Q.Y, zinv3, F);
    fe xo, yo;
    ec::from_mont<P256Curve>(xo, x, F);
    ec::from_mont<P256Curve>(yo, y, F);
    std::string out(64, '\0');
    ec::fe_to_be<P256Curve>((unsigned char*)out.data(), xo);
    ec::fe_to_be<P256Curve>((unsigned char*)out.data() + 32, yo);
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
