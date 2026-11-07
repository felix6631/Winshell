#include <string.h>
#include "hash.h"

#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

size_t hash_size(HashAlgorithm a) {
    switch (a) {
        case HASH_MD5:    return 16;
        case HASH_SHA1:   return 20;
        case HASH_SHA224: return 28;
        case HASH_SHA256: return 32;
        case HASH_SHA384: return 48;
        default:          return 64;
    }
}

static size_t block_size(HashAlgorithm a) {
    return a == HASH_SHA384 || a == HASH_SHA512 ? 128 : 64;
}

/* ---- MD5 ----------------------------------------------------------------------------- */

static const uint32_t md5K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};
static const int md5S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static void md5_block(uint32_t* state, const unsigned char* p) {
    uint32_t m[16], a = state[0], b = state[1], c = state[2], d = state[3];
    int i;

    for (i = 0; i < 16; i++)
        m[i] = (uint32_t)p[i * 4] | ((uint32_t)p[i * 4 + 1] << 8) | ((uint32_t)p[i * 4 + 2] << 16) | ((uint32_t)p[i * 4 + 3] << 24);
    for (i = 0; i < 64; i++) {
        uint32_t f, t;
        int g;

        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) & 15;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) & 15;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) & 15;
        }
        t = d;
        d = c;
        c = b;
        b = b + ROL32(a + f + md5K[i] + m[g], md5S[i]);
        a = t;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

/* ---- SHA-1 --------------------------------------------------------------------------- */

static void sha1_block(uint32_t* state, const unsigned char* p) {
    uint32_t w[80], a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    int i;

    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
    for (i = 16; i < 80; i++)
        w[i] = ROL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (i = 0; i < 80; i++) {
        uint32_t f, k, t;

        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5a827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdc;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6;
        }
        t = ROL32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROL32(b, 30);
        b = a;
        a = t;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

/* ---- SHA-256 (and 224) --------------------------------------------------------------- */

static const uint32_t sha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_block(uint32_t* state, const unsigned char* p) {
    uint32_t w[64], v[8];
    int i;

    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, state, sizeof v);
    for (i = 0; i < 64; i++) {
        uint32_t s1 = ROR32(v[4], 6) ^ ROR32(v[4], 11) ^ ROR32(v[4], 25);
        uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint32_t t1 = v[7] + s1 + ch + sha256K[i] + w[i];
        uint32_t s0 = ROR32(v[0], 2) ^ ROR32(v[0], 13) ^ ROR32(v[0], 22);
        uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        uint32_t t2 = s0 + maj;

        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++)
        state[i] += v[i];
}

/* ---- SHA-512 (and 384) --------------------------------------------------------------- */

static const uint64_t sha512K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
    0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
    0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
    0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
    0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
    0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

static void sha512_block(uint64_t* state, const unsigned char* p) {
    uint64_t w[80], v[8];
    int i, k;

    for (i = 0; i < 16; i++) {
        w[i] = 0;
        for (k = 0; k < 8; k++)
            w[i] = (w[i] << 8) | p[i * 8 + k];
    }
    for (i = 16; i < 80; i++) {
        uint64_t s0 = ROR64(w[i - 15], 1) ^ ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = ROR64(w[i - 2], 19) ^ ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, state, sizeof v);
    for (i = 0; i < 80; i++) {
        uint64_t s1 = ROR64(v[4], 14) ^ ROR64(v[4], 18) ^ ROR64(v[4], 41);
        uint64_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint64_t t1 = v[7] + s1 + ch + sha512K[i] + w[i];
        uint64_t s0 = ROR64(v[0], 28) ^ ROR64(v[0], 34) ^ ROR64(v[0], 39);
        uint64_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        uint64_t t2 = s0 + maj;

        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++)
        state[i] += v[i];
}

/* ---- the interface ------------------------------------------------------------------- */

void hash_init(HashContext* c, HashAlgorithm a) {
    memset(c, 0, sizeof *c);
    c->algorithm = a;
    switch (a) {
        case HASH_MD5:
            c->state.w32[0] = 0x67452301; c->state.w32[1] = 0xefcdab89; c->state.w32[2] = 0x98badcfe; c->state.w32[3] = 0x10325476;
            break;
        case HASH_SHA1:
            c->state.w32[0] = 0x67452301; c->state.w32[1] = 0xefcdab89; c->state.w32[2] = 0x98badcfe;
            c->state.w32[3] = 0x10325476; c->state.w32[4] = 0xc3d2e1f0;
            break;
        case HASH_SHA224: {
            static const uint32_t iv[8] = { 0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4 };
            memcpy(c->state.w32, iv, sizeof iv);
            break;
        }
        case HASH_SHA256: {
            static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
            memcpy(c->state.w32, iv, sizeof iv);
            break;
        }
        case HASH_SHA384: {
            static const uint64_t iv[8] = { 0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
                                            0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL };
            memcpy(c->state.w64, iv, sizeof iv);
            break;
        }
        default: {
            static const uint64_t iv[8] = { 0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                                            0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL };
            memcpy(c->state.w64, iv, sizeof iv);
            break;
        }
    }
}

static void process_block(HashContext* c, const unsigned char* p) {
    switch (c->algorithm) {
        case HASH_MD5:    md5_block(c->state.w32, p); break;
        case HASH_SHA1:   sha1_block(c->state.w32, p); break;
        case HASH_SHA224:
        case HASH_SHA256: sha256_block(c->state.w32, p); break;
        default:          sha512_block(c->state.w64, p); break;
    }
}

void hash_update(HashContext* c, const void* data, size_t length) {
    const unsigned char* p = data;
    size_t size = block_size(c->algorithm);

    c->length += length;
    if (c->blockLength > 0) {
        size_t take = size - c->blockLength;

        if (take > length)
            take = length;
        memcpy(c->block + c->blockLength, p, take);
        c->blockLength += take;
        p += take;
        length -= take;
        if (c->blockLength < size)
            return;
        process_block(c, c->block);
        c->blockLength = 0;
    }
    while (length >= size) {
        process_block(c, p);
        p += size;
        length -= size;
    }
    if (length > 0) {
        memcpy(c->block, p, length);
        c->blockLength = length;
    }
}

void hash_final(HashContext* c, unsigned char* out) {
    size_t size = block_size(c->algorithm), lengthBytes = size == 128 ? 16 : 8, i;
    uint64_t bits = c->length * 8;
    unsigned char pad[256];
    size_t padLength;
    int little = c->algorithm == HASH_MD5;

    // 0x80, zeros, and the length in bits (little endian for MD5, big endian for the others)
    padLength = (c->blockLength < size - lengthBytes ? size - lengthBytes - c->blockLength : 2 * size - lengthBytes - c->blockLength);
    memset(pad, 0, sizeof pad);
    pad[0] = 0x80;
    for (i = 0; i < 8; i++)
        pad[padLength + (little ? i : lengthBytes - 1 - i)] = (unsigned char)(bits >> (8 * i));
    {
        uint64_t keepLength = c->length;

        hash_update(c, pad, padLength + lengthBytes);
        c->length = keepLength;
    }
    if (little) {
        for (i = 0; i < 16; i++)
            out[i] = (unsigned char)(c->state.w32[i / 4] >> (8 * (i % 4)));
    } else if (size == 64) {
        size_t n = hash_size(c->algorithm);

        for (i = 0; i < n; i++)
            out[i] = (unsigned char)(c->state.w32[i / 4] >> (24 - 8 * (i % 4)));
    } else {
        size_t n = hash_size(c->algorithm);

        for (i = 0; i < n; i++)
            out[i] = (unsigned char)(c->state.w64[i / 8] >> (56 - 8 * (i % 8)));
    }
}
