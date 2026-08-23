// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "x25519.hpp"

#include <cstddef>
#include <cstdint>

// X25519 (RFC 7748) from scratch. Field elements of GF(2^255 - 19) are 16 limbs of 16 bits
// in int64 lanes (the compact TweetNaCl-style representation): wide enough that schoolbook
// multiplication cannot overflow before the carry pass, small enough to stay readable.
// EVERYTHING here is constant-time in the secret scalar: fixed 255 ladder steps, arithmetic
// conditional swaps (no branches on secret bits), and a fixed carry/reduce schedule.

namespace cheatah::x25519 {
namespace {

using i64 = std::int64_t;
using Fe = i64[16];  // one field element: 16 little-endian 16-bit limbs

// Propagate carries so every limb fits 16 bits again; the top carry re-enters at limb 0
// multiplied by 38 (= 2 * 19, because 2^256 = 38 mod p). @complexity O(1) @alloc none
// @test CheatahX25519.Rfc7748Vector1
void carry(Fe o) {
    for (std::size_t i = 0; i < 16; ++i) {
        o[i] += (1LL << 16);
        const i64 c = o[i] >> 16;
        o[(i + 1) * static_cast<std::size_t>(i < 15)] += c - 1 + 37 * (c - 1) * static_cast<i64>(i == 15);
        o[i] -= c << 16;
    }
}

// Constant-time conditional swap: when b == 1 swap (p, q), when b == 0 leave them — by
// XOR masking, never by branching on b (b derives from a SECRET scalar bit).
// @complexity O(1) @alloc none @test CheatahX25519.Rfc7748Vector2
void cswap(Fe p, Fe q, i64 b) {
    const i64 mask = ~(b - 1);
    for (int i = 0; i < 16; ++i) {
        const i64 t = mask & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

// o = a + b (no carry; callers carry after multiplication). @complexity O(1) @alloc none
// @test CheatahX25519.Rfc7748Vector1
void add(Fe o, const Fe a, const Fe b) {
    for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i];
}

// o = a - b. @complexity O(1) @alloc none @test CheatahX25519.Rfc7748Vector1
void sub(Fe o, const Fe a, const Fe b) {
    for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i];
}

// o = a * b mod p: schoolbook product, fold the high half back with *38, then two carry
// passes restore the limb bounds. @complexity O(1) @alloc none @test CheatahX25519.Rfc7748Vector1
void mul(Fe o, const Fe a, const Fe b) {
    i64 t[31] = {0};
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
    carry(o);
    carry(o);
}

// o = a^2 mod p. @complexity O(1) @alloc none @test CheatahX25519.Rfc7748Vector1
void sqr(Fe o, const Fe a) { mul(o, a, a); }

// o = z^-1 mod p by Fermat: z^(p-2), the standard fixed square-and-multiply chain (the two
// skipped indices 2 and 4 encode p-2's binary form). Constant-time: fixed 254 iterations.
// @complexity O(1) @alloc none @test CheatahX25519.DiffieHellman
void invert(Fe o, const Fe z) {
    Fe c;
    for (int i = 0; i < 16; ++i) c[i] = z[i];
    for (int i = 253; i >= 0; --i) {
        sqr(c, c);
        if (i != 2 && i != 4) mul(c, c, z);
    }
    for (int i = 0; i < 16; ++i) o[i] = c[i];
}

// Freeze to the canonical representative in [0, p) and serialize to 32 little-endian bytes.
// The two trial subtractions of p use arithmetic selection (no data-dependent branch).
// @complexity O(1) @alloc none @test CheatahX25519.Rfc7748Vector1
void pack(unsigned char out[32], const Fe n) {
    Fe t, m;
    for (int i = 0; i < 16; ++i) t[i] = n[i];
    carry(t);
    carry(t);
    carry(t);
    for (int rep = 0; rep < 2; ++rep) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const i64 borrow = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        cswap(t, m, 1 - borrow);
    }
    for (std::size_t i = 0; i < 16; ++i) {
        out[2 * i] = static_cast<unsigned char>(t[i] & 0xff);
        out[2 * i + 1] = static_cast<unsigned char>(t[i] >> 8);
    }
}

// Parse 32 little-endian bytes into limbs; the top bit is MASKED OFF per RFC 7748.
// @complexity O(1) @alloc none @test CheatahX25519.Rfc7748Vector1
void unpack(Fe o, const unsigned char in[32]) {
    for (std::size_t i = 0; i < 16; ++i) o[i] = in[2 * i] + (static_cast<i64>(in[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}

// The X25519 scalar multiplication: clamp the scalar, then 255 Montgomery-ladder steps over
// the u-coordinate only (x/z pairs), each step one cswap + the fixed add/sub/mul schedule
// with a24 = 121665. @complexity O(1) @alloc none @test CheatahX25519.Rfc7748Vector1
void scalarmult(unsigned char out[32], const unsigned char scalar[32], const unsigned char point[32]) {
    unsigned char z[32];
    for (int i = 0; i < 32; ++i) z[i] = scalar[i];
    z[0] &= 248;          // clamp: clear the low 3 bits (cofactor),
    z[31] &= 127;         // clear the top bit,
    z[31] |= 64;          // set bit 254.

    Fe x, a, b, c, d, e, f;
    static const Fe k121665 = {0xDB41, 1};
    unpack(x, point);
    for (int i = 0; i < 16; ++i) {
        b[i] = x[i];
        a[i] = c[i] = d[i] = 0;
    }
    a[0] = d[0] = 1;

    for (int i = 254; i >= 0; --i) {
        const i64 bit = (z[i >> 3] >> (i & 7)) & 1;
        cswap(a, b, bit);
        cswap(c, d, bit);
        add(e, a, c);
        sub(a, a, c);
        add(c, b, d);
        sub(b, b, d);
        sqr(d, e);
        sqr(f, a);
        mul(a, c, a);
        mul(c, b, e);
        add(e, a, c);
        sub(a, a, c);
        sqr(b, a);
        sub(c, d, f);
        mul(a, c, k121665);
        add(a, a, d);
        mul(c, c, a);
        mul(a, d, f);
        mul(d, b, x);
        sqr(b, e);
        cswap(a, b, bit);
        cswap(c, d, bit);
    }
    invert(c, c);
    mul(a, a, c);
    pack(out, a);
}

// ---- byte/hex helpers (the ed25519 module's conventions) ----

// 64-char lowercase/uppercase hex -> 32 bytes; false on any malformed input.
// @complexity O(1) @alloc none @test CheatahX25519.RejectsMalformed
bool hex32(std::string_view hex, unsigned char out[32]) {
    if (hex.size() != 64) return false;
    for (int i = 0; i < 32; ++i) {
        unsigned v = 0;
        for (int k = 0; k < 2; ++k) {
            const char ch = hex[2 * i + k];
            v <<= 4;
            if (ch >= '0' && ch <= '9') v |= static_cast<unsigned>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') v |= static_cast<unsigned>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') v |= static_cast<unsigned>(ch - 'A' + 10);
            else return false;
        }
        out[i] = static_cast<unsigned char>(v);
    }
    return true;
}

// 32 bytes -> 64-char lowercase hex. @complexity O(1) @alloc the returned string
// @test CheatahX25519.Rfc7748Vector1
std::string hex_of(const unsigned char in[32]) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(64);
    for (std::size_t i = 0; i < 32; ++i) {
        out[2 * i] = kHex[in[i] >> 4];
        out[2 * i + 1] = kHex[in[i] & 0xF];
    }
    return out;
}

} // namespace

std::string x25519(std::string_view scalar_hex, std::string_view point_hex) {
    unsigned char scalar[32], point[32], out[32];
    if (!hex32(scalar_hex, scalar) || !hex32(point_hex, point)) return "";
    scalarmult(out, scalar, point);
    unsigned char acc = 0;  // contributory check: an all-zero shared secret is rejected
    for (unsigned char i : out) acc |= i;
    if (acc == 0) return "";
    return hex_of(out);
}

std::string x25519_base(std::string_view scalar_hex) {
    unsigned char scalar[32], out[32];
    if (!hex32(scalar_hex, scalar)) return "";
    static const unsigned char kBase[32] = {9};
    scalarmult(out, scalar, kBase);
    return hex_of(out);
}

} // namespace cheatah::x25519
