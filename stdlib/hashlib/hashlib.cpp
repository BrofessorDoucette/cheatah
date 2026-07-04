// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "hashlib.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cheatah::hashlib {

namespace {

inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

constexpr std::uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline std::uint64_t rotr64(std::uint64_t x, unsigned n) {
    return (x >> n) | (x << (64 - n));
}

// SHA-512 round constants (first 64 bits of the fractional parts of the cube roots of
// the first 80 primes) and initial hash values (square roots of the first 8 primes).
constexpr std::uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

// to_hex / from_hex are this module's PUBLIC canonical byte<->hex helpers (defined after this
// anonymous namespace, declared in hashlib.hpp). The digest paths below and the tls / ed25519 /
// x509 modules all share that one implementation instead of re-rolling their own.

// SHA-256 core: returns the raw 32-byte digest.
std::array<std::uint8_t, 32> sha256_raw(std::string_view data) {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    // Pad: append 0x80, then zeros, then the 64-bit big-endian bit length.
    const std::uint64_t bitlen = static_cast<std::uint64_t>(data.size()) * 8;
    std::vector<std::uint8_t> msg(data.begin(), data.end());
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<std::uint8_t>(bitlen >> (i * 8)));

    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[off + i * 4]) << 24) |
                   (static_cast<std::uint32_t>(msg[off + i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[off + i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(msg[off + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<std::uint8_t>(h[i] >> ((3 - j) * 8));
    return out;
}

// SHA-512 core: returns the raw 64-byte digest.
std::array<std::uint8_t, 64> sha512_raw(std::string_view data) {
    std::uint64_t h[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
                          0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
                          0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};

    // Pad: 0x80, zeros to a 128-byte block leaving 16 bytes, then the 128-bit big-endian
    // bit length (the high 64 bits are always 0 for our inputs).
    const std::uint64_t bitlen = static_cast<std::uint64_t>(data.size()) * 8;
    std::vector<std::uint8_t> msg(data.begin(), data.end());
    msg.push_back(0x80);
    while (msg.size() % 128 != 112) msg.push_back(0x00);
    for (int i = 0; i < 8; ++i) msg.push_back(0x00);  // high 64 bits of the length
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<std::uint8_t>(bitlen >> (i * 8)));

    for (std::size_t off = 0; off < msg.size(); off += 128) {
        std::uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int j = 0; j < 8; ++j)
                w[i] = (w[i] << 8) | msg[off + i * 8 + j];
        }
        for (int i = 16; i < 80; ++i) {
            const std::uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const std::uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint64_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint64_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; ++i) {
            const std::uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
            const std::uint64_t ch = (e & f) ^ (~e & g);
            const std::uint64_t t1 = hh + S1 + ch + K512[i] + w[i];
            const std::uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
            const std::uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint64_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::array<std::uint8_t, 64> out{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) out[i * 8 + j] = static_cast<std::uint8_t>(h[i] >> ((7 - j) * 8));
    return out;
}

} // namespace

std::string sha256(std::string_view data) {
    const auto d = sha256_raw(data);
    return to_hex(d.data(), d.size());
}

std::string sha512(std::string_view data) {
    const auto d = sha512_raw(data);
    return to_hex(d.data(), d.size());
}

std::string sha256_digest(std::string_view data) {
    const auto d = sha256_raw(data);
    return std::string(reinterpret_cast<const char*>(d.data()), d.size());
}

std::string sha512_digest(std::string_view data) {
    const auto d = sha512_raw(data);
    return std::string(reinterpret_cast<const char*>(d.data()), d.size());
}

// ---- HMAC + HKDF (RFC 2104 / RFC 5869) over the SHA-256 above ----------------------

std::string hmac_sha256(std::string_view key, std::string_view data) {
    constexpr std::size_t kBlock = 64;  // SHA-256 block size
    std::string k(key);
    if (k.size() > kBlock) k = sha256_digest(k);  // long keys hash down first
    k.resize(kBlock, '\0');
    std::string inner(kBlock, '\0');
    std::string outer(kBlock, '\0');
    for (std::size_t i = 0; i < kBlock; ++i) {
        inner[i] = static_cast<char>(k[i] ^ 0x36);
        outer[i] = static_cast<char>(k[i] ^ 0x5c);
    }
    return sha256_digest(outer + sha256_digest(inner + std::string(data)));
}

std::string hmac_sha512(std::string_view key, std::string_view data) {
    constexpr std::size_t kBlock = 128;  // SHA-512 block size
    std::string k(key);
    if (k.size() > kBlock) k = sha512_digest(k);  // long keys hash down first
    k.resize(kBlock, '\0');
    std::string inner(kBlock, '\0');
    std::string outer(kBlock, '\0');
    for (std::size_t i = 0; i < kBlock; ++i) {
        inner[i] = static_cast<char>(k[i] ^ 0x36);
        outer[i] = static_cast<char>(k[i] ^ 0x5c);
    }
    return sha512_digest(outer + sha512_digest(inner + std::string(data)));
}

// ---- Hex (the one canonical byte<->hex for the whole crypto stack) -----------------

std::string to_hex(const std::uint8_t* data, std::size_t n) {
    static const char hexd[] = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(hexd[data[i] >> 4]);
        out.push_back(hexd[data[i] & 0xF]);
    }
    return out;
}

std::string to_hex(std::string_view bytes) {
    return to_hex(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

std::string from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0) throw std::invalid_argument("hashlib::from_hex: odd-length hex");
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) throw std::invalid_argument("hashlib::from_hex: non-hex character");
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

// ---- Base64 (RFC 4648, standard alphabet) -----------------------------------------

namespace {
constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}  // namespace

std::string base64_encode(std::string_view data) {
    const auto* p = reinterpret_cast<const unsigned char*>(data.data());
    const std::size_t n = data.size();
    std::string out;
    out.reserve(((n + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= n; i += 3) {
        const unsigned v = (static_cast<unsigned>(p[i]) << 16) |
                           (static_cast<unsigned>(p[i + 1]) << 8) | static_cast<unsigned>(p[i + 2]);
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(kB64[(v >> 6) & 63]);
        out.push_back(kB64[v & 63]);
    }
    if (n - i == 1) {
        const unsigned v = static_cast<unsigned>(p[i]) << 16;
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (n - i == 2) {
        const unsigned v =
            (static_cast<unsigned>(p[i]) << 16) | (static_cast<unsigned>(p[i + 1]) << 8);
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(kB64[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::string base64_decode(std::string_view text, bool strict) {
    std::array<int, 256> rev;
    rev.fill(-1);
    for (int i = 0; i < 64; ++i) rev[static_cast<unsigned char>(kB64[i])] = i;
    std::string out;
    out.reserve((text.size() / 4) * 3);
    int buf = 0;
    int bits = 0;
    for (unsigned char c : text) {
        if (c == '=') break;             // padding -> end of data
        const int d = rev[c];
        if (d < 0) {
            // Whitespace is always skipped. Any OTHER non-alphabet byte is silently ignored in the
            // default (lenient) mode, but REJECTED — empty result — in strict mode. X.509 PEM parsing
            // uses strict so a malformed body fails closed instead of decoding to garbage.
            if (strict && c != ' ' && c != '\n' && c != '\r' && c != '\t') return std::string();
            continue;                    // skip whitespace / (lenient) non-alphabet bytes
        }
        buf = (buf << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xff));
        }
    }
    return out;
}

std::string hkdf_extract(std::string_view salt, std::string_view ikm) {
    return hmac_sha256(salt, ikm);
}

std::string hkdf_expand(std::string_view prk, std::string_view info, long long length) {
    if (length <= 0 || length > 255 * 32) return "";
    std::string okm;
    std::string t;  // T(0) = empty
    for (unsigned char counter = 1; static_cast<long long>(okm.size()) < length; ++counter) {
        t = hmac_sha256(prk, t + std::string(info) + static_cast<char>(counter));
        okm += t;
    }
    okm.resize(static_cast<std::size_t>(length));
    return okm;
}

} // namespace cheatah::hashlib
