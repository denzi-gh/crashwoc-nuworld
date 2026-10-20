#include "vfs/sha256.h"

#include <string.h>

static const uint32_t nw_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void nw_sha256_block(uint32_t state[8], const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + nw_sha256_k[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void nw_sha256_init(NwSha256 *s)
{
    static const uint32_t iv[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    memcpy(s->state, iv, sizeof(iv));
    s->length = 0;
}

void nw_sha256_update(NwSha256 *s, const void *data, size_t size)
{
    const uint8_t *p = data;
    size_t used = (size_t)(s->length % 64);
    s->length += size;

    if (used > 0) {
        size_t n = 64 - used < size ? 64 - used : size;
        memcpy(s->block + used, p, n);
        p += n;
        size -= n;
        if (used + n < 64) {
            return;
        }
        nw_sha256_block(s->state, s->block);
    }
    for (; size >= 64; p += 64, size -= 64) {
        nw_sha256_block(s->state, p);
    }
    memcpy(s->block, p, size);
}

void nw_sha256_final(NwSha256 *s, uint8_t digest[NW_SHA256_SIZE])
{
    uint64_t bits = s->length * 8;
    size_t used = (size_t)(s->length % 64);

    s->block[used++] = 0x80;
    if (used > 56) {
        memset(s->block + used, 0, 64 - used);
        nw_sha256_block(s->state, s->block);
        used = 0;
    }
    memset(s->block + used, 0, 56 - used);
    for (int i = 0; i < 8; i++) {
        s->block[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    nw_sha256_block(s->state, s->block);

    for (int i = 0; i < 8; i++) {
        digest[4 * i] = (uint8_t)(s->state[i] >> 24);
        digest[4 * i + 1] = (uint8_t)(s->state[i] >> 16);
        digest[4 * i + 2] = (uint8_t)(s->state[i] >> 8);
        digest[4 * i + 3] = (uint8_t)s->state[i];
    }
}

void nw_sha256(const void *data, size_t size, uint8_t digest[NW_SHA256_SIZE])
{
    NwSha256 s;
    nw_sha256_init(&s);
    nw_sha256_update(&s, data, size);
    nw_sha256_final(&s, digest);
}

void nw_sha256_hex(const uint8_t digest[NW_SHA256_SIZE], char hex[NW_SHA256_HEX])
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < NW_SHA256_SIZE; i++) {
        hex[2 * i] = digits[digest[i] >> 4];
        hex[2 * i + 1] = digits[digest[i] & 15];
    }
    hex[2 * NW_SHA256_SIZE] = '\0';
}
