/**
 * @file blake2s.c
 * @brief BLAKE2s (see blake2s.h).
 */

#include "blake2s.h"

#include "common/memory.h"

static const uint32_t iv[8] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19,
};

static const uint8_t sigma[10][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
};

static uint32_t rotr(uint32_t v, int n)
{
    return v >> n | v << (32 - n);
}

static uint32_t load32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void mix(uint32_t *v, int a, int b, int c, int d, uint32_t x, uint32_t y)
{
    v[a] = v[a] + v[b] + x;
    v[d] = rotr(v[d] ^ v[a], 16);
    v[c] = v[c] + v[d];
    v[b] = rotr(v[b] ^ v[c], 12);
    v[a] = v[a] + v[b] + y;
    v[d] = rotr(v[d] ^ v[a], 8);
    v[c] = v[c] + v[d];
    v[b] = rotr(v[b] ^ v[c], 7);
}

static void compress(blake2s_t *s, const uint8_t *block, bool last)
{
    uint32_t m[16];
    uint32_t v[16];
    for (int i = 0; i < 16; i++)
    {
        m[i] = load32(block + 4 * i);
    }
    for (int i = 0; i < 8; i++)
    {
        v[i] = s->h[i];
        v[8 + i] = iv[i];
    }
    v[12] ^= s->t[0];
    v[13] ^= s->t[1];
    if (last)
    {
        v[14] = ~v[14];
    }
    for (int round = 0; round < 10; round++)
    {
        const uint8_t *p = sigma[round];
        mix(v, 0, 4, 8, 12, m[p[0]], m[p[1]]);
        mix(v, 1, 5, 9, 13, m[p[2]], m[p[3]]);
        mix(v, 2, 6, 10, 14, m[p[4]], m[p[5]]);
        mix(v, 3, 7, 11, 15, m[p[6]], m[p[7]]);
        mix(v, 0, 5, 10, 15, m[p[8]], m[p[9]]);
        mix(v, 1, 6, 11, 12, m[p[10]], m[p[11]]);
        mix(v, 2, 7, 8, 13, m[p[12]], m[p[13]]);
        mix(v, 3, 4, 9, 14, m[p[14]], m[p[15]]);
    }
    for (int i = 0; i < 8; i++)
    {
        s->h[i] ^= v[i] ^ v[8 + i];
    }
}

static void count(blake2s_t *s, uint32_t n)
{
    s->t[0] += n;
    if (s->t[0] < n)
    {
        s->t[1]++;
    }
}

void blake2s_init(blake2s_t *s, uint32_t out_length)
{
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < 8; i++)
    {
        s->h[i] = iv[i];
    }
    s->h[0] ^= 0x01010000 ^ out_length; // depth 1, fanout 1, no key
    s->out_length = out_length;
}

void blake2s_update(blake2s_t *s, const void *data, uint32_t length)
{
    const uint8_t *p = data;
    while (length > 0)
    {
        // the last block is kept back: final compresses it with the last flag
        if (s->buffered == BLAKE2S_BLOCK_SIZE)
        {
            count(s, BLAKE2S_BLOCK_SIZE);
            compress(s, s->buffer, false);
            s->buffered = 0;
        }
        uint32_t n = BLAKE2S_BLOCK_SIZE - s->buffered;
        if (n > length)
        {
            n = length;
        }
        memcpy(s->buffer + s->buffered, p, n);
        s->buffered += n;
        p += n;
        length -= n;
    }
}

void blake2s_final(blake2s_t *s, uint8_t *out)
{
    count(s, s->buffered);
    memset(s->buffer + s->buffered, 0, BLAKE2S_BLOCK_SIZE - s->buffered);
    compress(s, s->buffer, true);
    uint8_t full[BLAKE2S_HASH_SIZE];
    for (int i = 0; i < 8; i++)
    {
        full[4 * i] = (uint8_t)s->h[i];
        full[4 * i + 1] = (uint8_t)(s->h[i] >> 8);
        full[4 * i + 2] = (uint8_t)(s->h[i] >> 16);
        full[4 * i + 3] = (uint8_t)(s->h[i] >> 24);
    }
    memcpy(out, full, s->out_length);
    memset(full, 0, sizeof(full));
    memset(s, 0, sizeof(*s));
}
