#include <string>
#include "utils/crypto_utils.hpp"
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdint>

namespace muvik {
namespace crypto {

namespace {

// ============================================================================
// 1. MD5 实现
// ============================================================================
struct MD5Context {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t buffer[64];
};

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define FF(a, b, c, d, x, s, ac) { \
    (a) += F((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = ROTATE_LEFT((a), (s)); \
    (a) += (b); \
}
#define GG(a, b, c, d, x, s, ac) { \
    (a) += G((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = ROTATE_LEFT((a), (s)); \
    (a) += (b); \
}
#define HH(a, b, c, d, x, s, ac) { \
    (a) += H((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = ROTATE_LEFT((a), (s)); \
    (a) += (b); \
}
#define II(a, b, c, d, x, s, ac) { \
    (a) += I((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = ROTATE_LEFT((a), (s)); \
    (a) += (b); \
}

void md5Transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    for (int i = 0, j = 0; j < 64; ++i, j += 4) {
        x[i] = ((uint32_t)block[j]) | (((uint32_t)block[j + 1]) << 8) |
               (((uint32_t)block[j + 2]) << 16) | (((uint32_t)block[j + 3]) << 24);
    }

    // Round 1
    FF(a, b, c, d, x[ 0],  7, 0xd76aa478);
    FF(d, a, b, c, x[ 1], 12, 0xe8c7b756);
    FF(c, d, a, b, x[ 2], 17, 0x242070db);
    FF(b, c, d, a, x[ 3], 22, 0xc1bdceee);
    FF(a, b, c, d, x[ 4],  7, 0xf57c0faf);
    FF(d, a, b, c, x[ 5], 12, 0x4787c62a);
    FF(c, d, a, b, x[ 6], 17, 0xa8304613);
    FF(b, c, d, a, x[ 7], 22, 0xfd469501);
    FF(a, b, c, d, x[ 8],  7, 0x698098d8);
    FF(d, a, b, c, x[ 9], 12, 0x8b44f7af);
    FF(c, d, a, b, x[10], 17, 0xffff5bb1);
    FF(b, c, d, a, x[11], 22, 0x895cd7be);
    FF(a, b, c, d, x[12],  7, 0x6b901122);
    FF(d, a, b, c, x[13], 12, 0xfd987193);
    FF(c, d, a, b, x[14], 17, 0xa679438e);
    FF(b, c, d, a, x[15], 22, 0x49b40821);

    // Round 2
    GG(a, b, c, d, x[ 1],  5, 0xf61e2562);
    GG(d, a, b, c, x[ 6],  9, 0xc040b340);
    GG(c, d, a, b, x[11], 14, 0x265e5a51);
    GG(b, c, d, a, x[ 0], 20, 0xe9b6c7aa);
    GG(a, b, c, d, x[ 5],  5, 0xd62f105d);
    GG(d, a, b, c, x[10],  9, 0x02441453);
    GG(c, d, a, b, x[15], 14, 0xd8a1e681);
    GG(b, c, d, a, x[ 4], 20, 0xe7d3fbc8);
    GG(a, b, c, d, x[ 9],  5, 0x21e1cde6);
    GG(d, a, b, c, x[14],  9, 0xc33707d6);
    GG(c, d, a, b, x[ 3], 14, 0xf4d50d87);
    GG(b, c, d, a, x[ 8], 20, 0x455a14ed);
    GG(a, b, c, d, x[13],  5, 0xa9e3e905);
    GG(d, a, b, c, x[ 2],  9, 0xfcefa3f8);
    GG(c, d, a, b, x[ 7], 14, 0x676f02d9);
    GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    // Round 3
    HH(a, b, c, d, x[ 5],  4, 0xfffa3942);
    HH(d, a, b, c, x[ 8], 11, 0x8771f681);
    HH(c, d, a, b, x[11], 16, 0x6d9d6122);
    HH(b, c, d, a, x[14], 23, 0xfde5380c);
    HH(a, b, c, d, x[ 1],  4, 0xa4beea44);
    HH(d, a, b, c, x[ 4], 11, 0x4bdecfa9);
    HH(c, d, a, b, x[ 7], 16, 0xf6bb4b60);
    HH(b, c, d, a, x[10], 23, 0xbebfbc70);
    HH(a, b, c, d, x[13],  4, 0x289b7ec6);
    HH(d, a, b, c, x[ 0], 11, 0xeaa127fa);
    HH(c, d, a, b, x[ 3], 16, 0xd4ef3085);
    HH(b, c, d, a, x[ 6], 23, 0x04881d05);
    HH(a, b, c, d, x[ 9],  4, 0xd9d4d039);
    HH(d, a, b, c, x[12], 11, 0xe6db99e5);
    HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
    HH(b, c, d, a, x[ 2], 23, 0xc4ac5665);

    // Round 4
    II(a, b, c, d, x[ 0],  6, 0xf4292244);
    II(d, a, b, c, x[ 7], 10, 0x432aff97);
    II(c, d, a, b, x[14], 15, 0xab9423a7);
    II(b, c, d, a, x[ 5], 21, 0xfc93a039);
    II(a, b, c, d, x[12],  6, 0x655b59c3);
    II(d, a, b, c, x[ 3], 10, 0x8f0ccc92);
    II(c, d, a, b, x[10], 15, 0xffeff47d);
    II(b, c, d, a, x[ 1], 21, 0x85845dd1);
    II(a, b, c, d, x[ 8],  6, 0x6fa87e4f);
    II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    II(c, d, a, b, x[ 6], 15, 0xa3014314);
    II(b, c, d, a, x[13], 21, 0x4e0811a1);
    II(a, b, c, d, x[ 4],  6, 0xf7537e82);
    II(d, a, b, c, x[11], 10, 0xbd3af235);
    II(c, d, a, b, x[ 2], 15, 0x2ad7d2bb);
    II(b, c, d, a, x[ 9], 21, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void md5Init(MD5Context& ctx) {
    ctx.count[0] = ctx.count[1] = 0;
    ctx.state[0] = 0x67452301;
    ctx.state[1] = 0xefcdab89;
    ctx.state[2] = 0x98badcfe;
    ctx.state[3] = 0x10325476;
}

void md5Update(MD5Context& ctx, const uint8_t* input, size_t input_len) {
    uint32_t idx = (ctx.count[0] >> 3) & 0x3F;
    if ((ctx.count[0] += ((uint32_t)input_len << 3)) < ((uint32_t)input_len << 3)) {
        ctx.count[1]++;
    }
    ctx.count[1] += ((uint32_t)input_len >> 29);

    uint32_t part_len = 64 - idx;
    size_t i = 0;
    if (input_len >= part_len) {
        std::memcpy(&ctx.buffer[idx], input, part_len);
        md5Transform(ctx.state, ctx.buffer);
        for (i = part_len; i + 63 < input_len; i += 64) {
            md5Transform(ctx.state, &input[i]);
        }
        idx = 0;
    }
    std::memcpy(&ctx.buffer[idx], &input[i], input_len - i);
}

void md5Final(uint8_t digest[16], MD5Context& ctx) {
    static const uint8_t PADDING[64] = { 0x80 };
    uint8_t bits[8];
    for (int i = 0; i < 4; ++i) {
        bits[i] = (uint8_t)((ctx.count[0] >> (i * 8)) & 0xFF);
        bits[i + 4] = (uint8_t)((ctx.count[1] >> (i * 8)) & 0xFF);
    }
    uint32_t idx = (ctx.count[0] >> 3) & 0x3F;
    uint32_t pad_len = (idx < 56) ? (56 - idx) : (120 - idx);
    md5Update(ctx, PADDING, pad_len);
    md5Update(ctx, bits, 8);
    for (int i = 0; i < 4; ++i) {
        digest[i * 4 + 0] = (uint8_t)((ctx.state[i] >>  0) & 0xFF);
        digest[i * 4 + 1] = (uint8_t)((ctx.state[i] >>  8) & 0xFF);
        digest[i * 4 + 2] = (uint8_t)((ctx.state[i] >> 16) & 0xFF);
        digest[i * 4 + 3] = (uint8_t)((ctx.state[i] >> 24) & 0xFF);
    }
}

// ============================================================================
// 2. SHA-256 实现
// ============================================================================
struct SHA256Context {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROR(x, 2) ^ ROR(x, 13) ^ ROR(x, 22))
#define EP1(x) (ROR(x, 6) ^ ROR(x, 11) ^ ROR(x, 25))
#define SIG0(x) (ROR(x, 7) ^ ROR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROR(x, 17) ^ ROR(x, 19) ^ ((x) >> 10))

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

void sha256Transform(uint32_t state[8], const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    for (int i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    for (int i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + K256[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256Init(SHA256Context& ctx) {
    ctx.count = 0;
    ctx.state[0] = 0x6a09e667; ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372; ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f; ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab; ctx.state[7] = 0x5be0cd19;
}

void sha256Update(SHA256Context& ctx, const uint8_t* data, size_t len) {
    size_t i = 0;
    size_t idx = (size_t)(ctx.count & 0x3F);
    ctx.count += len;
    if (idx) {
        size_t part_len = 64 - idx;
        if (len >= part_len) {
            std::memcpy(&ctx.buffer[idx], data, part_len);
            sha256Transform(ctx.state, ctx.buffer);
            i = part_len;
        } else {
            std::memcpy(&ctx.buffer[idx], data, len);
            return;
        }
    }
    for (; i + 63 < len; i += 64) {
        sha256Transform(ctx.state, &data[i]);
    }
    std::memcpy(ctx.buffer, &data[i], len - i);
}

void sha256Final(uint8_t digest[32], SHA256Context& ctx) {
    uint8_t pad[64] = { 0x80 };
    uint64_t bit_len = ctx.count * 8;
    uint8_t len_bytes[8];
    for (int i = 0; i < 8; ++i) {
        len_bytes[7 - i] = (uint8_t)(bit_len >> (i * 8));
    }
    size_t idx = (size_t)(ctx.count & 0x3F);
    size_t pad_len = (idx < 56) ? (56 - idx) : (120 - idx);
    sha256Update(ctx, pad, pad_len);
    sha256Update(ctx, len_bytes, 8);
    for (int i = 0; i < 8; ++i) {
        digest[i * 4 + 0] = (uint8_t)(ctx.state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx.state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx.state[i] >>  8);
        digest[i * 4 + 3] = (uint8_t)(ctx.state[i] >>  0);
    }
}

std::string toHex(const uint8_t* data, size_t len) {
    static const char HEX[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(HEX[(data[i] >> 4) & 0x0F]);
        out.push_back(HEX[data[i] & 0x0F]);
    }
    return out;
}

// ============================================================================
// 3. Base64 实现
// ============================================================================
static const char B64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

std::string md5(const std::string& input) {
    MD5Context ctx;
    md5Init(ctx);
    md5Update(ctx, reinterpret_cast<const uint8_t*>(input.data()), input.size());
    uint8_t digest[16];
    md5Final(digest, ctx);
    return toHex(digest, 16);
}

std::string sha256(const std::string& input) {
    SHA256Context ctx;
    sha256Init(ctx);
    sha256Update(ctx, reinterpret_cast<const uint8_t*>(input.data()), input.size());
    uint8_t digest[32];
    sha256Final(digest, ctx);
    return toHex(digest, 32);
}

std::string hmacSha256(const std::string& key, const std::string& data) {
    uint8_t k[64] = { 0 };
    if (key.size() > 64) {
        SHA256Context ctx;
        sha256Init(ctx);
        sha256Update(ctx, reinterpret_cast<const uint8_t*>(key.data()), key.size());
        sha256Final(k, ctx);
    } else {
        std::memcpy(k, key.data(), key.size());
    }

    uint8_t k_ipad[64];
    uint8_t k_opad[64];
    for (int i = 0; i < 64; ++i) {
        k_ipad[i] = k[i] ^ 0x36;
        k_opad[i] = k[i] ^ 0x5c;
    }

    SHA256Context inner;
    sha256Init(inner);
    sha256Update(inner, k_ipad, 64);
    sha256Update(inner, reinterpret_cast<const uint8_t*>(data.data()), data.size());
    uint8_t inner_hash[32];
    sha256Final(inner_hash, inner);

    SHA256Context outer;
    sha256Init(outer);
    sha256Update(outer, k_opad, 64);
    sha256Update(outer, inner_hash, 32);
    uint8_t outer_hash[32];
    sha256Final(outer_hash, outer);

    return toHex(outer_hash, 32);
}

std::string base64Encode(const std::string& input) {
    std::string out;
    size_t len = input.size();
    out.reserve(((len + 2) / 3) * 4);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(input.data());

    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (bytes[i] << 16);
        if (i + 1 < len) val |= (bytes[i + 1] << 8);
        if (i + 2 < len) val |= bytes[i + 2];

        out.push_back(B64_CHARS[(val >> 18) & 0x3F]);
        out.push_back(B64_CHARS[(val >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? B64_CHARS[(val >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? B64_CHARS[val & 0x3F] : '=');
    }
    return out;
}

std::string base64Decode(const std::string& input) {
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; ++i) T[(unsigned char)B64_CHARS[i]] = i;

    std::string out;
    uint32_t val = 0;
    int valb = -8;
    for (char c : input) {
        if (T[(unsigned char)c] == -1) {
            if (c == '=') break;
            continue;
        }
        val = (val << 6) | T[(unsigned char)c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

} // namespace crypto
} // namespace muvik
