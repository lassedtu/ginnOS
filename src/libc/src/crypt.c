/**
 * @file crypt.c
 * @brief SHA-256 (FIPS 180-4) and the SHA-crypt "$5$" password scheme
 *        (Ulrich Drepper's specification, as in glibc).
 *
 * this file uses only the standard C headers, so the host tests build it
 * with the host's libc and compare it with "openssl passwd -5".
 */

#include "crypt.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SALT_MAX       16
#define ROUNDS_DEFAULT 5000
#define ROUNDS_MIN     1000
#define ROUNDS_MAX     999999999UL

typedef struct
{
    uint32_t h[8];
    uint64_t length; // bytes
    uint8_t block[64];
    size_t used;
} sha256_t;

static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
};

static uint32_t ror(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void compress(sha256_t *s, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
               (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++)
    {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++)
    {
        uint32_t sum1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        uint32_t t1 = h + sum1 + ((e & f) ^ (~e & g)) + k[i] + w[i];
        uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    s->h[5] += f;
    s->h[6] += g;
    s->h[7] += h;
}

static void sha_init(sha256_t *s)
{
    static const uint32_t start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, start, sizeof(start));
    s->length = 0;
    s->used = 0;
}

static void sha_add(sha256_t *s, const void *data, size_t length)
{
    const uint8_t *p = data;
    s->length += length;
    while (length > 0)
    {
        size_t take = 64 - s->used < length ? 64 - s->used : length;
        memcpy(s->block + s->used, p, take);
        s->used += take;
        p += take;
        length -= take;
        if (s->used == 64)
        {
            compress(s, s->block);
            s->used = 0;
        }
    }
}

static void sha_end(sha256_t *s, uint8_t out[32])
{
    uint64_t bits = s->length * 8;
    uint8_t pad = 0x80;
    sha_add(s, &pad, 1);
    pad = 0;
    while (s->used != 56)
        sha_add(s, &pad, 1);
    uint8_t size[8];
    for (int i = 0; i < 8; i++)
        size[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha_add(s, size, 8);
    for (int i = 0; i < 8; i++)
    {
        out[4 * i] = (uint8_t)(s->h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
        out[4 * i + 3] = (uint8_t)s->h[i];
    }
}

void sha256(const void *data, size_t length, uint8_t out[32])
{
    sha256_t s;
    sha_init(&s);
    sha_add(&s, data, length);
    sha_end(&s, out);
}

static const char b64[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/** n characters of the 24 bits b2 b1 b0, the low bits first */
static char *put64(char *out, uint8_t b2, uint8_t b1, uint8_t b0, int n)
{
    uint32_t w = (uint32_t)b2 << 16 | (uint32_t)b1 << 8 | b0;
    while (n-- > 0)
    {
        *out++ = b64[w & 0x3f];
        w >>= 6;
    }
    return out;
}

/** add a digest again and again to make length bytes */
static void add_repeated(sha256_t *s, const uint8_t digest[32], size_t length)
{
    for (; length > 32; length -= 32)
        sha_add(s, digest, 32);
    sha_add(s, digest, length);
}

char *crypt(const char *key, const char *setting)
{
    static char out[3 + 17 + SALT_MAX + 1 + 43 + 1];
    if (!key || !setting || strncmp(setting, "$5$", 3) != 0)
        return NULL;
    const char *salt = setting + 3;
    unsigned long rounds = ROUNDS_DEFAULT;
    bool custom = false;
    if (strncmp(salt, "rounds=", 7) == 0)
    {
        char *end;
        rounds = strtoul(salt + 7, &end, 10);
        if (*end != '$')
            return NULL;
        rounds = rounds < ROUNDS_MIN ? ROUNDS_MIN : rounds > ROUNDS_MAX ? ROUNDS_MAX : rounds;
        custom = true;
        salt = end + 1;
    }
    size_t salt_length = strcspn(salt, "$");
    salt_length = salt_length > SALT_MAX ? SALT_MAX : salt_length;
    size_t key_length = strlen(key);

    // B: key, salt, key
    sha256_t s;
    uint8_t b[32];
    sha_init(&s);
    sha_add(&s, key, key_length);
    sha_add(&s, salt, salt_length);
    sha_add(&s, key, key_length);
    sha_end(&s, b);

    // A: key, salt, B for each byte of the key, then B or the key for each bit
    uint8_t a[32];
    sha_init(&s);
    sha_add(&s, key, key_length);
    sha_add(&s, salt, salt_length);
    add_repeated(&s, b, key_length);
    for (size_t n = key_length; n > 0; n >>= 1)
    {
        if (n & 1)
            sha_add(&s, b, 32);
        else
            sha_add(&s, key, key_length);
    }
    sha_end(&s, a);

    // P: the key hashed key_length times, as long as the key
    uint8_t dp[32];
    sha_init(&s);
    for (size_t i = 0; i < key_length; i++)
        sha_add(&s, key, key_length);
    sha_end(&s, dp);
    uint8_t *p = malloc(key_length ? key_length : 1);
    if (!p)
        return NULL;
    for (size_t i = 0; i < key_length; i++)
        p[i] = dp[i % 32];

    // S: the salt hashed 16 + A[0] times, as long as the salt
    uint8_t ds[32];
    sha_init(&s);
    for (int i = 0; i < 16 + a[0]; i++)
        sha_add(&s, salt, salt_length);
    sha_end(&s, ds);
    uint8_t sb[SALT_MAX];
    for (size_t i = 0; i < salt_length; i++)
        sb[i] = ds[i];

    // the rounds: A again and again with P and S
    for (unsigned long r = 0; r < rounds; r++)
    {
        sha_init(&s);
        if (r & 1)
            sha_add(&s, p, key_length);
        else
            sha_add(&s, a, 32);
        if (r % 3)
            sha_add(&s, sb, salt_length);
        if (r % 7)
            sha_add(&s, p, key_length);
        if (r & 1)
            sha_add(&s, a, 32);
        else
            sha_add(&s, p, key_length);
        sha_end(&s, a);
    }
    memset(p, 0, key_length);
    free(p);

    char *o = out;
    o += sprintf(o, "$5$");
    if (custom)
        o += sprintf(o, "rounds=%lu$", rounds);
    memcpy(o, salt, salt_length);
    o += salt_length;
    *o++ = '$';
    static const uint8_t order[10][3] = {{0, 10, 20},  {21, 1, 11}, {12, 22, 2}, {3, 13, 23},
                                         {24, 4, 14},  {15, 25, 5}, {6, 16, 26}, {27, 7, 17},
                                         {18, 28, 8},  {9, 19, 29}};
    for (int i = 0; i < 10; i++)
        o = put64(o, a[order[i][0]], a[order[i][1]], a[order[i][2]], 4);
    o = put64(o, 0, a[31], a[30], 3);
    *o = '\0';

    // no copy of the key's hashes stays on the stack
    memset(b, 0, sizeof(b));
    memset(dp, 0, sizeof(dp));
    return out;
}

int crypt_verify(const char *key, const char *hash)
{
    if (!hash)
        return 0;
    const char *made = crypt(key, hash);
    if (!made)
        return 0;
    size_t length = strlen(made);
    if (strlen(hash) != length)
        return 0;
    unsigned char differ = 0;
    for (size_t i = 0; i < length; i++)
        differ |= (unsigned char)(made[i] ^ hash[i]);
    return differ == 0;
}

static uint64_t cycles(void)
{
#if defined(__i386__) || defined(__x86_64__)
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return (uint64_t)high << 32 | low;
#else
    return (uint64_t)clock();
#endif
}

char *crypt_new(const char *key, const void *seed, size_t seed_length)
{
    struct
    {
        uint64_t cycles;
        int64_t time;
        int32_t pid;
        uintptr_t stack;
    } mix = {cycles(), (int64_t)time(NULL), (int32_t)getpid(), (uintptr_t)&mix};
    sha256_t s;
    uint8_t digest[32];
    sha_init(&s);
    sha_add(&s, &mix, sizeof(mix));
    if (seed && seed_length)
        sha_add(&s, seed, seed_length);
    sha_end(&s, digest);

    char setting[3 + SALT_MAX + 2] = "$5$";
    for (int i = 0; i < SALT_MAX; i++)
        setting[3 + i] = b64[digest[i] & 0x3f];
    setting[3 + SALT_MAX] = '$';
    setting[3 + SALT_MAX + 1] = '\0';
    return crypt(key, setting);
}
