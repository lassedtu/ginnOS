/**
 * @file inet.c
 * @brief IPv4 addresses as text (arpa/inet.h).
 */

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

int inet_pton(int af, const char *src, void *dst)
{
    if (af != AF_INET)
    {
        errno = EAFNOSUPPORT;
        return -1;
    }
    // exactly four decimal parts, 0 to 255, no leading zeros
    uint8_t parts[4];
    for (int i = 0; i < 4; i++)
    {
        if (*src < '0' || *src > '9')
        {
            return 0;
        }
        unsigned value = 0;
        int digits = 0;
        while (*src >= '0' && *src <= '9')
        {
            if (digits > 0 && value == 0)
            {
                return 0; // a leading zero
            }
            value = value * 10 + (unsigned)(*src++ - '0');
            if (++digits > 3 || value > 255)
            {
                return 0;
            }
        }
        parts[i] = (uint8_t)value;
        if (i < 3 && *src++ != '.')
        {
            return 0;
        }
    }
    if (*src != '\0')
    {
        return 0;
    }
    memcpy(dst, parts, 4);
    return 1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    if (af != AF_INET)
    {
        errno = EAFNOSUPPORT;
        return NULL;
    }
    const uint8_t *p = src;
    char text[INET_ADDRSTRLEN];
    int n = snprintf(text, sizeof(text), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    if (n < 0 || (socklen_t)n >= size)
    {
        errno = ENOSPC;
        return NULL;
    }
    memcpy(dst, text, (size_t)n + 1);
    return dst;
}

int inet_aton(const char *text, struct in_addr *out)
{
    unsigned long parts[4];
    int count = 0;
    const char *p = text;
    for (;;)
    {
        if (*p < '0' || *p > '9')
        {
            return 0;
        }
        char *end;
        parts[count++] = strtoul(p, &end, 0);
        p = end;
        if (*p != '.' || count == 4)
        {
            break;
        }
        p++;
    }
    if (*p != '\0')
    {
        return 0;
    }
    // the last part fills the bytes the parts before it leave
    unsigned long value = 0;
    static const unsigned long last_max[4] = {0xFFFFFFFFul, 0xFFFFFFul, 0xFFFFul, 0xFFul};
    for (int i = 0; i < count - 1; i++)
    {
        if (parts[i] > 0xFF)
        {
            return 0;
        }
        value |= parts[i] << (24 - 8 * i);
    }
    if (parts[count - 1] > last_max[count - 1])
    {
        return 0;
    }
    value |= parts[count - 1];
    if (out)
    {
        out->s_addr = htonl((uint32_t)value);
    }
    return 1;
}

in_addr_t inet_addr(const char *text)
{
    struct in_addr a;
    return inet_aton(text, &a) ? a.s_addr : INADDR_NONE;
}

char *inet_ntoa(struct in_addr in)
{
    static __thread char text[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &in, text, sizeof(text));
    return text;
}
