/**
 * @file random.c
 * @brief random bytes for programs: getrandom, getentropy and the
 *        arc4random family, all from the kernel's generator.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/random.h>
#include <syscall.h>
#include <unistd.h>

ssize_t getrandom(void *buf, size_t length, unsigned int flags)
{
    long ret = _syscall(SYS_GETRANDOM, (long)buf, (long)length, (long)flags, 0, 0);
    if (ret < 0)
    {
        errno = (int)-ret;
        return -1;
    }
    return (ssize_t)ret;
}

int getentropy(void *buf, size_t length)
{
    if (length > 256)
    {
        errno = EIO;
        return -1;
    }
    unsigned char *p = buf;
    while (length > 0)
    {
        ssize_t n = getrandom(p, length, 0);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        p += n;
        length -= (size_t)n;
    }
    return 0;
}

void arc4random_buf(void *buf, size_t length)
{
    unsigned char *p = buf;
    while (length > 0)
    {
        ssize_t n = getrandom(p, length, 0);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            abort(); // arc4random cannot fail
        }
        p += n;
        length -= (size_t)n;
    }
}

uint32_t arc4random(void)
{
    uint32_t value;
    arc4random_buf(&value, sizeof(value));
    return value;
}

uint32_t arc4random_uniform(uint32_t bound)
{
    if (bound < 2)
    {
        return 0;
    }
    // values below min are thrown away, so each result is equally likely
    uint32_t min = -bound % bound;
    for (;;)
    {
        uint32_t value = arc4random();
        if (value >= min)
        {
            return value % bound;
        }
    }
}
