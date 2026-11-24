/**
 * @file random.c
 * @brief libctest: random bytes (getrandom, getentropy, arc4random,
 *        /dev/urandom).
 */

#include "libctest.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

void test_random(void)
{
    unsigned char a[64];
    unsigned char b[64];
    CHECK(getrandom(a, sizeof(a), 0) == (ssize_t)sizeof(a));
    CHECK(getrandom(b, sizeof(b), GRND_NONBLOCK) == (ssize_t)sizeof(b));
    CHECK(memcmp(a, b, sizeof(a)) != 0);
    CHECK(getrandom(a, sizeof(a), 0x80) == -1 && errno == EINVAL);
    CHECK(getrandom(a, sizeof(a), GRND_RANDOM | GRND_INSECURE) == -1 && errno == EINVAL);
    CHECK(getrandom((void *)1, 16, 0) == -1 && errno == EFAULT);

    CHECK(getentropy(a, sizeof(a)) == 0);
    CHECK(getentropy(a, 257) == -1 && errno == EIO);

    // a big read: about half the bits are set
    static unsigned char big[65536];
    CHECK(getrandom(big, sizeof(big), 0) == (ssize_t)sizeof(big));
    long ones = 0;
    for (size_t i = 0; i < sizeof(big); i++)
    {
        ones += __builtin_popcount(big[i]);
    }
    CHECK(ones > 260000 && ones < 264288); // 262144, about 4 sigma either way

    int fd = open("/dev/urandom", O_RDWR);
    CHECK(fd >= 0 && read(fd, a, sizeof(a)) == (ssize_t)sizeof(a));
    CHECK(fd >= 0 && write(fd, "mix", 3) == 3);
    close(fd);
    fd = open("/dev/random", O_RDONLY);
    CHECK(fd >= 0 && read(fd, b, sizeof(b)) == (ssize_t)sizeof(b) &&
          memcmp(a, b, sizeof(a)) != 0);
    close(fd);

    // arc4random_uniform stays below its bound, and reaches each value
    int seen[10] = {0};
    int below = 1;
    for (int i = 0; i < 1000; i++)
    {
        uint32_t v = arc4random_uniform(10);
        below &= v < 10;
        if (v < 10)
        {
            seen[v]++;
        }
    }
    int all = 1;
    for (int i = 0; i < 10; i++)
    {
        all &= seen[i] > 0;
    }
    CHECK(below && all);
    CHECK(arc4random_uniform(1) == 0 && arc4random_uniform(0) == 0);
    CHECK(arc4random() != arc4random() || arc4random() != arc4random());
}
