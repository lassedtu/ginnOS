/**
 * @file test_random.c
 * @brief the random pool: ChaCha20 and BLAKE2s against their RFC vectors,
 *        and the generator.
 */

#include "ktest.h"

#include "common/memory.h"
#include "kernel/random/blake2s.h"
#include "kernel/random/chacha20.h"
#include "kernel/random/random.h"

KTEST(chacha20_block_matches_rfc_8439)
{
    // RFC 8439 section 2.3.2
    uint8_t key[32];
    for (int i = 0; i < 32; i++)
    {
        key[i] = (uint8_t)i;
    }
    static const uint8_t nonce[12] = {0, 0, 0, 0x09, 0, 0, 0, 0x4a, 0, 0, 0, 0};
    static const uint8_t expected[64] = {
        0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15, 0x50, 0x0f, 0xdd, 0x1f, 0xa3,
        0x20, 0x71, 0xc4, 0xc7, 0xd1, 0xf4, 0xc7, 0x33, 0xc0, 0x68, 0x03, 0x04, 0x22,
        0xaa, 0x9a, 0xc3, 0xd4, 0x6c, 0x4e, 0xd2, 0x82, 0x64, 0x46, 0x07, 0x9f, 0xaa,
        0x09, 0x14, 0xc2, 0xd7, 0x05, 0xd9, 0x8b, 0x02, 0xa2, 0xb5, 0x12, 0x9c, 0xd1,
        0xde, 0x16, 0x4e, 0xb9, 0xcb, 0xd0, 0x83, 0xe8, 0xa2, 0x50, 0x3c, 0x4e,
    };
    uint8_t out[64];
    chacha20_block(key, 1, nonce, out);
    KEXPECT(memcmp(out, expected, sizeof(out)) == 0);
}

KTEST(blake2s_matches_rfc_7693)
{
    // RFC 7693 appendix B: BLAKE2s-256("abc")
    static const uint8_t expected[32] = {
        0x50, 0x8C, 0x5E, 0x8C, 0x32, 0x7C, 0x14, 0xE2, 0xE1, 0xA7, 0x2B,
        0xA3, 0x4E, 0xEB, 0x45, 0x2F, 0x37, 0x45, 0x8B, 0x20, 0x9E, 0xD6,
        0x3A, 0x29, 0x4D, 0x99, 0x9B, 0x4C, 0x86, 0x67, 0x59, 0x82,
    };
    blake2s_t s;
    uint8_t out[32];
    blake2s_init(&s, 32);
    blake2s_update(&s, "abc", 3);
    blake2s_final(&s, out);
    KEXPECT(memcmp(out, expected, sizeof(out)) == 0);

    // the same input in pieces over a block border gives the same hash
    uint8_t data[200];
    for (int i = 0; i < 200; i++)
    {
        data[i] = (uint8_t)(i * 7);
    }
    uint8_t whole[32];
    uint8_t pieces[32];
    blake2s_init(&s, 32);
    blake2s_update(&s, data, sizeof(data));
    blake2s_final(&s, whole);
    blake2s_init(&s, 32);
    blake2s_update(&s, data, 63);
    blake2s_update(&s, data + 63, 2);
    blake2s_update(&s, data + 65, 135);
    blake2s_final(&s, pieces);
    KEXPECT(memcmp(whole, pieces, sizeof(whole)) == 0);
}

KTEST(random_gives_different_bytes_each_time)
{
    uint8_t a[100];
    uint8_t b[100];
    random_get(a, sizeof(a));
    random_get(b, sizeof(b));
    KEXPECT(memcmp(a, b, sizeof(a)) != 0);

    // a rough check: about half the bits of a long output are set
    uint8_t big[4096];
    random_get(big, sizeof(big));
    uint32_t ones = 0;
    for (uint32_t i = 0; i < sizeof(big); i++)
    {
        for (uint8_t v = big[i]; v; v &= (uint8_t)(v - 1))
        {
            ones++;
        }
    }
    KEXPECT(ones > 15800 && ones < 16968); // 16384 with 4.5 sigma either way
    KEXPECT(random_u32() != random_u32() || random_u32() != random_u32());
}
