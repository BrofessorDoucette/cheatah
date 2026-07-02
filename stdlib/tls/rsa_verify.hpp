#pragma once

// rsa_verify.hpp — minimal RSA-PSS (rsa_pss_rsae_sha256) signature verification for the TLS 1.3
// CertificateVerify, so servers with an RSA leaf certificate complete the handshake. Verification
// ONLY (no key generation, no signing): a small big-unsigned-integer (modexp with the public
// exponent), an RSA public-key extractor from the certificate's SubjectPublicKeyInfo DER, and
// EMSA-PSS-VERIFY (MGF1 + salt, both SHA-256). No OpenSSL — SHA-256 comes from hashlib. Header-only
// and `inline`, included by exactly one translation unit (tls.cpp).
//
// Scope: rsa_pss_rsae_sha256 only (SignatureScheme 0x0804) — the scheme a TLS 1.3 server with an RSA
// certificate uses for CertificateVerify. Salt length = hash length = 32 (per the scheme). The bignum
// is correctness-first (schoolbook multiply + bit-serial reduction); the public exponent is tiny
// (typically 65537), so a handful of modular squarings per handshake is negligible.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "hashlib.hpp"  // sha256_digest

namespace cheatah::tls::rsa {

// A big unsigned integer as little-endian 32-bit limbs (limb[0] = least significant); no trailing
// zero limbs (the empty vector is the value 0).
using Big = std::vector<std::uint32_t>;

inline void trim(Big& a) {
    while (!a.empty() && a.back() == 0) a.pop_back();
}

// Big from big-endian bytes (leading zero bytes are ignored).
inline Big from_be(const unsigned char* p, std::size_t n) {
    std::size_t s = 0;
    while (s < n && p[s] == 0) ++s;
    const std::size_t len = n - s;
    Big a((len + 3) / 4, 0);
    for (std::size_t i = 0; i < len; ++i) {
        const unsigned char b = p[n - 1 - i];  // i-th byte from the least significant end
        a[i / 4] |= static_cast<std::uint32_t>(b) << (8 * (i % 4));
    }
    trim(a);
    return a;
}
inline Big from_be(const std::string& s) {
    return from_be(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

inline std::size_t bitlen(const Big& a) {
    if (a.empty()) return 0;
    std::uint32_t top = a.back();
    std::size_t b = 0;
    while (top) {
        ++b;
        top >>= 1;
    }
    return (a.size() - 1) * 32 + b;
}
inline std::size_t bytelen(const Big& a) { return (bitlen(a) + 7) / 8; }

// Big to big-endian bytes, left-padded/truncated to exactly `outlen` bytes (caller ensures it fits).
inline std::string to_be(const Big& a, std::size_t outlen) {
    std::string out(outlen, '\0');
    for (std::size_t i = 0; i < a.size(); ++i)
        for (int b = 0; b < 4; ++b) {
            const std::size_t byteidx = i * 4 + b;  // from the least significant end
            if (byteidx >= outlen) continue;
            out[outlen - 1 - byteidx] = static_cast<char>((a[i] >> (8 * b)) & 0xff);
        }
    return out;
}

inline int cmp(const Big& a, const Big& b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (std::size_t i = a.size(); i-- > 0;)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

inline Big shl1(const Big& a) {
    Big r(a.size(), 0);
    std::uint32_t carry = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const std::uint64_t v = (static_cast<std::uint64_t>(a[i]) << 1) | carry;
        r[i] = static_cast<std::uint32_t>(v);
        carry = static_cast<std::uint32_t>(v >> 32);
    }
    if (carry) r.push_back(carry);
    trim(r);
    return r;
}

// a - b, assuming a >= b.
inline Big sub(const Big& a, const Big& b) {
    Big r(a.size(), 0);
    std::int64_t borrow = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const std::int64_t bi = i < b.size() ? static_cast<std::int64_t>(b[i]) : 0;
        std::int64_t v = static_cast<std::int64_t>(a[i]) - bi - borrow;
        if (v < 0) {
            v += (static_cast<std::int64_t>(1) << 32);
            borrow = 1;
        } else {
            borrow = 0;
        }
        r[i] = static_cast<std::uint32_t>(v);
    }
    trim(r);
    return r;
}

inline Big mul(const Big& a, const Big& b) {
    if (a.empty() || b.empty()) return {};
    Big r(a.size() + b.size(), 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const std::uint64_t cur = static_cast<std::uint64_t>(r[i + j]) +
                                      static_cast<std::uint64_t>(a[i]) * b[j] + carry;
            r[i + j] = static_cast<std::uint32_t>(cur);
            carry = cur >> 32;
        }
        r[i + b.size()] = static_cast<std::uint32_t>(r[i + b.size()] + carry);
    }
    trim(r);
    return r;
}

// x mod m, via bit-serial long division (correctness-first; called a few dozen times per handshake).
inline Big mod(const Big& x, const Big& m) {
    Big r;  // 0
    for (std::size_t bi = x.size() * 32; bi-- > 0;) {
        const std::uint32_t bit = (x[bi / 32] >> (bi % 32)) & 1u;
        r = shl1(r);
        if (bit) {
            if (r.empty())
                r.push_back(1);
            else
                r[0] |= 1u;
        }
        if (cmp(r, m) >= 0) r = sub(r, m);
    }
    return r;
}

inline Big mulmod(const Big& a, const Big& b, const Big& m) { return mod(mul(a, b), m); }

// base^e mod m (left-to-right square-and-multiply; e is small for RSA verification).
inline Big modexp(Big base, const Big& e, const Big& m) {
    base = mod(base, m);
    Big result;
    result.push_back(1);
    const std::size_t bits = e.size() * 32;
    std::size_t top = 0;
    bool found = false;
    for (std::size_t bi = bits; bi-- > 0;)
        if ((e[bi / 32] >> (bi % 32)) & 1u) {
            top = bi;
            found = true;
            break;
        }
    if (!found) return result;  // e == 0 -> 1
    for (std::size_t bi = top + 1; bi-- > 0;) {
        result = mulmod(result, result, m);
        if ((e[bi / 32] >> (bi % 32)) & 1u) result = mulmod(result, base, m);
    }
    return result;
}

// ---- DER: read a length octet sequence at `p` (advances p); SIZE_MAX on error ----
inline std::size_t der_len(const std::string& d, std::size_t& p) {
    if (p >= d.size()) return static_cast<std::size_t>(-1);
    const unsigned char b = static_cast<unsigned char>(d[p++]);
    if (!(b & 0x80)) return b;  // short form
    const int n = b & 0x7f;
    if (n == 0 || n > 4 || p + static_cast<std::size_t>(n) > d.size()) return static_cast<std::size_t>(-1);
    std::size_t L = 0;
    for (int i = 0; i < n; ++i) L = (L << 8) | static_cast<unsigned char>(d[p++]);
    return L;
}

// Extract the RSA public key (modulus n, exponent e as big-endian bytes) from a certificate's
// SubjectPublicKeyInfo: find the rsaEncryption OID (1.2.840.113549.1.1.1), then the BIT STRING wrapping
// RSAPublicKey ::= SEQUENCE { INTEGER n, INTEGER e }. Returns false when the cert key is not RSA.
inline bool parse_rsa_pubkey(const std::string& der, std::string& n, std::string& e) {
    static const unsigned char kOid[] = {0x06, 0x09, 0x2A, 0x86, 0x48, 0x86,
                                         0xF7, 0x0D, 0x01, 0x01, 0x01};
    std::size_t pos = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i + sizeof kOid <= der.size(); ++i)
        if (std::memcmp(der.data() + i, kOid, sizeof kOid) == 0) {
            pos = i;
            break;
        }
    if (pos == static_cast<std::size_t>(-1)) return false;
    std::size_t i = pos + sizeof kOid;
    if (i + 1 < der.size() && static_cast<unsigned char>(der[i]) == 0x05 &&
        static_cast<unsigned char>(der[i + 1]) == 0x00)
        i += 2;  // skip the NULL algorithm parameters
    if (i >= der.size() || static_cast<unsigned char>(der[i]) != 0x03) return false;  // BIT STRING
    ++i;
    if (der_len(der, i) == static_cast<std::size_t>(-1)) return false;
    if (i >= der.size() || static_cast<unsigned char>(der[i]) != 0x00) return false;  // 0 unused bits
    ++i;
    if (i >= der.size() || static_cast<unsigned char>(der[i]) != 0x30) return false;  // SEQUENCE
    ++i;
    if (der_len(der, i) == static_cast<std::size_t>(-1)) return false;
    if (i >= der.size() || static_cast<unsigned char>(der[i]) != 0x02) return false;  // INTEGER n
    ++i;
    const std::size_t nl = der_len(der, i);
    if (nl == static_cast<std::size_t>(-1) || i + nl > der.size()) return false;
    n = der.substr(i, nl);
    i += nl;
    if (i >= der.size() || static_cast<unsigned char>(der[i]) != 0x02) return false;  // INTEGER e
    ++i;
    const std::size_t el = der_len(der, i);
    if (el == static_cast<std::size_t>(-1) || i + el > der.size()) return false;
    e = der.substr(i, el);
    auto strip = [](std::string& s) {
        std::size_t k = 0;
        while (k + 1 < s.size() && static_cast<unsigned char>(s[k]) == 0) ++k;
        s = s.substr(k);
    };
    strip(n);
    strip(e);
    return !n.empty() && !e.empty();
}

// MGF1 with SHA-256 (RFC 8017): the mask-generation function for PSS.
inline std::string mgf1_sha256(const std::string& seed, std::size_t mask_len) {
    std::string T;
    std::uint32_t counter = 0;
    while (T.size() < mask_len) {
        const unsigned char c[4] = {static_cast<unsigned char>(counter >> 24),
                                    static_cast<unsigned char>(counter >> 16),
                                    static_cast<unsigned char>(counter >> 8),
                                    static_cast<unsigned char>(counter)};
        T += hashlib::sha256_digest(seed + std::string(reinterpret_cast<const char*>(c), 4));
        ++counter;
    }
    T.resize(mask_len);
    return T;
}

// EMSA-PSS-VERIFY (RFC 8017 §9.1.2) with SHA-256 and salt length 32. `em` is the encoded message of
// `em_bits` bits (length ceil(em_bits/8)); `m_hash` is SHA-256 of the signed content.
inline bool pss_verify(const std::string& m_hash, const std::string& em, std::size_t em_bits) {
    const std::size_t hlen = 32, slen = 32;
    const std::size_t em_len = (em_bits + 7) / 8;
    if (em.size() != em_len) return false;
    if (em_len < hlen + slen + 2) return false;
    if (static_cast<unsigned char>(em[em_len - 1]) != 0xbc) return false;
    const std::size_t db_len = em_len - hlen - 1;
    std::string masked_db = em.substr(0, db_len);
    const std::string H = em.substr(db_len, hlen);
    const std::size_t top_bits = 8 * em_len - em_bits;
    if (top_bits > 0) {
        const unsigned char mask = static_cast<unsigned char>(0xFF << (8 - top_bits));
        if (static_cast<unsigned char>(masked_db[0]) & mask) return false;
    }
    const std::string db_mask = mgf1_sha256(H, db_len);
    std::string db(db_len, '\0');
    for (std::size_t i = 0; i < db_len; ++i)
        db[i] = static_cast<char>(static_cast<unsigned char>(masked_db[i]) ^
                                  static_cast<unsigned char>(db_mask[i]));
    if (top_bits > 0)
        db[0] = static_cast<char>(static_cast<unsigned char>(db[0]) & (0xFF >> top_bits));
    const std::size_t ps_len = db_len - slen - 1;
    for (std::size_t i = 0; i < ps_len; ++i)
        if (static_cast<unsigned char>(db[i]) != 0) return false;
    if (static_cast<unsigned char>(db[ps_len]) != 0x01) return false;
    const std::string salt = db.substr(ps_len + 1);  // slen bytes
    const std::string mprime = std::string(8, '\0') + m_hash + salt;
    return hashlib::sha256_digest(mprime) == H;
}

// Verify a TLS 1.3 CertificateVerify RSA-PSS (rsa_pss_rsae_sha256) signature: recover EM = sig^e mod n
// with the certificate's RSA public key, then EMSA-PSS-VERIFY against SHA-256(`message`).
inline bool verify_pss_sha256(const std::string& cert_der, const std::string& message,
                              const std::string& sig) {
    std::string nb, eb;
    if (!parse_rsa_pubkey(cert_der, nb, eb)) return false;
    const Big n = from_be(nb), e = from_be(eb);
    if (n.empty() || e.empty()) return false;
    if (sig.size() != nb.size()) return false;  // signature length must equal the modulus length
    const Big s = from_be(sig);
    if (cmp(s, n) >= 0) return false;  // signature representative out of range
    const Big m = modexp(s, e, n);
    const std::size_t em_bits = bitlen(n) - 1;     // RFC 8017: emBits = modBits - 1
    const std::size_t em_len = (em_bits + 7) / 8;
    if (bytelen(m) > em_len) return false;
    const std::string em = to_be(m, em_len);
    return pss_verify(hashlib::sha256_digest(message), em, em_bits);
}

}  // namespace cheatah::tls::rsa
