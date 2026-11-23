/**
 * @file test_audio.c
 * @brief the mixing of the sound streams: sums and clipping.
 */

#include "ktest.h"

#include "kernel/audio/audio.h"

static void put(uint8_t *p, int16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)((uint16_t)v >> 8);
}

static int16_t get(const uint8_t *p)
{
    return (int16_t)(p[0] | (p[1] << 8));
}

KTEST(audio_mix_adds_streams)
{
    uint8_t a[8];
    uint8_t b[8];
    put(a, 1000);
    put(a + 2, -1000);
    put(a + 4, 30000);
    put(a + 6, -30000);
    put(b, 234);
    put(b + 2, -234);
    put(b + 4, 10000);
    put(b + 6, -10000);

    int32_t sums[4] = {0};
    audio_mix_add(sums, a, 4);
    audio_mix_add(sums, b, 4);
    uint8_t out[8];
    audio_mix_out(out, sums, 4);
    KEXPECT_EQ(get(out), 1234);
    KEXPECT_EQ(get(out + 2), -1234);
    KEXPECT_EQ(get(out + 4), 32767);  // clipped
    KEXPECT_EQ(get(out + 6), -32768); // clipped
}

KTEST(audio_mix_of_one_stream_is_the_same)
{
    uint8_t a[4];
    put(a, -32768);
    put(a + 2, 32767);
    int32_t sums[2] = {0};
    audio_mix_add(sums, a, 2);
    uint8_t out[4];
    audio_mix_out(out, sums, 2);
    KEXPECT_EQ(get(out), -32768);
    KEXPECT_EQ(get(out + 2), 32767);
}
