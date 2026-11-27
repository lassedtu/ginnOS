#pragma once

/**
 * @file byteorder.h
 * @brief network byte order (big endian) on the little-endian x86.
 */

#include "common/stdint.h"

static inline uint16_t net_htons(uint16_t v)
{
    return __builtin_bswap16(v);
}

static inline uint16_t net_ntohs(uint16_t v)
{
    return __builtin_bswap16(v);
}

static inline uint32_t net_htonl(uint32_t v)
{
    return __builtin_bswap32(v);
}

static inline uint32_t net_ntohl(uint32_t v)
{
    return __builtin_bswap32(v);
}

/**
 * an IPv4 address in network byte order from its four parts (a.b.c.d).
 */
#define NET_IPV4(a, b, c, d)                                                                \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
