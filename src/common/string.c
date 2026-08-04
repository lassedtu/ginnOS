#include "string.h"

uint32_t strlen(const char *str)
{
    uint32_t length = 0;

    while (*str++)
    {
        length++;
    }

    return length;
}

int strcmp(const char *lhs, const char *rhs)
{
    while (*lhs != 0 && *lhs == *rhs)
    {
        lhs++;
        rhs++;
    }

    return (int)(unsigned char)*lhs - (int)(unsigned char)*rhs;
}

int strncmp(const char *lhs, const char *rhs, uint32_t count)
{
    while (count-- > 0)
    {
        if (*lhs != *rhs || *lhs == 0 || *rhs == 0)
        {
            return (int)(unsigned char)*lhs - (int)(unsigned char)*rhs;
        }

        lhs++;
        rhs++;
    }

    return 0;
}

char *strcpy(char *dest, const char *src)
{
    char *out = dest;

    while (*src)
    {
        *dest++ = *src++;
    }

    *dest = 0;
    return out;
}

char *strncpy(char *dest, const char *src, uint32_t count)
{
    char *out = dest;

    while (count > 0 && *src)
    {
        *dest++ = *src++;
        count--;
    }

    while (count > 0)
    {
        *dest++ = 0;
        count--;
    }

    return out;
}
