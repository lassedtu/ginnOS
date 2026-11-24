/**
 * @file chacha20.c
 * @brief the ChaCha20 block function (see chacha20.h).
 */

#include "chacha20.h"

static uint32_t load32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void store32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t rotl(uint32_t v, int n)
{
    return v << n | v >> (32 - n);
}

static void quarter_round(uint32_t *x, int a, int b, int c, int d)
{
    x[a] += x[b];
    x[d] = rotl(x[d] ^ x[a], 16);
    x[c] += x[d];
    x[b] = rotl(x[b] ^ x[c], 12);
    x[a] += x[b];
    x[d] = rotl(x[d] ^ x[a], 8);
    x[c] += x[d];
    x[b] = rotl(x[b] ^ x[c], 7);
}

void chacha20_block(const uint8_t key[CHACHA20_KEY_SIZE], uint32_t counter,
                    const uint8_t nonce[12], uint8_t out[CHACHA20_BLOCK_SIZE])
{
    uint32_t state[16];
    state[0] = 0x61707865; // "expand 32-byte k"
    state[1] = 0x3320646e;
    state[2] = 0x79622d32;
    state[3] = 0x6b206574;
    for (int i = 0; i < 8; i++)
    {
        state[4 + i] = load32(key + 4 * i);
    }
    state[12] = counter;
    for (int i = 0; i < 3; i++)
    {
        state[13 + i] = load32(nonce + 4 * i);
    }

    uint32_t x[16];
    for (int i = 0; i < 16; i++)
    {
        x[i] = state[i];
    }
    for (int round = 0; round < 10; round++)
    {
        // a column round, then a diagonal round
        quarter_round(x, 0, 4, 8, 12);
        quarter_round(x, 1, 5, 9, 13);
        quarter_round(x, 2, 6, 10, 14);
        quarter_round(x, 3, 7, 11, 15);
        quarter_round(x, 0, 5, 10, 15);
        quarter_round(x, 1, 6, 11, 12);
        quarter_round(x, 2, 7, 8, 13);
        quarter_round(x, 3, 4, 9, 14);
    }
    for (int i = 0; i < 16; i++)
    {
        store32(out + 4 * i, x[i] + state[i]);
    }
}
