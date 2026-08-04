#include "memory.h"

void *memcpy(void *dest, const void *src, uint32_t count)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    while (count--)
    {
        *d++ = *s++;
    }

    return dest;
}

void *memmove(void *dest, const void *src, uint32_t count)
{
    uint8_t *d;
    const uint8_t *s;

    if (dest == src || count == 0)
    {
        return dest;
    }

    d = (uint8_t *)dest;
    s = (const uint8_t *)src;

    if (d < s)
    {
        while (count--)
        {
            *d++ = *s++;
        }

        return dest;
    }

    d += count;
    s += count;

    while (count--)
    {
        *--d = *--s;
    }

    return dest;
}

void *memset(void *dest, int value, uint32_t count)
{
    uint8_t *d = (uint8_t *)dest;

    while (count--)
    {
        *d++ = (uint8_t)value;
    }

    return dest;
}

int memcmp(const void *lhs, const void *rhs, uint32_t count)
{
    const uint8_t *a = (const uint8_t *)lhs;
    const uint8_t *b = (const uint8_t *)rhs;

    while (count--)
    {
        if (*a != *b)
        {
            return (int)*a - (int)*b;
        }

        a++;
        b++;
    }

    return 0;
}
