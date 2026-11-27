#pragma once

#include <stdint.h>
#include <sys/socket.h>

/**
 * @file netinet/in.h
 * @brief IPv4 addresses and ports, and the IP protocols.
 *
 * the IPv6 types are here for programs that know both families. there is
 * no IPv6 network: socket(AF_INET6, ...) gives EAFNOSUPPORT, and programs
 * then use IPv4.
 *
 * addresses and ports in the structs are in network byte order (big
 * endian); htons, htonl, ntohs and ntohl change between it and the CPU's.
 */

typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

struct in_addr
{
    in_addr_t s_addr;
};

struct sockaddr_in
{
    sa_family_t sin_family; // AF_INET
    in_port_t sin_port;
    struct in_addr sin_addr;
    unsigned char sin_zero[8];
};

struct in6_addr
{
    uint8_t s6_addr[16];
};

struct sockaddr_in6
{
    sa_family_t sin6_family; // AF_INET6
    in_port_t sin6_port;
    uint32_t sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t sin6_scope_id;
};

extern const struct in6_addr in6addr_any;      // ::
extern const struct in6_addr in6addr_loopback; // ::1

#define IN6ADDR_ANY_INIT      {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}
#define IN6ADDR_LOOPBACK_INIT {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}}

#define IPPROTO_IP   0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17
#define IPPROTO_IPV6 41

#define IPV6_V6ONLY 26

#define INADDR_ANY       ((in_addr_t)0x00000000)
#define INADDR_BROADCAST ((in_addr_t)0xFFFFFFFF)
#define INADDR_NONE      ((in_addr_t)0xFFFFFFFF)
#define INADDR_LOOPBACK  ((in_addr_t)0x7F000001) // 127.0.0.1, in host byte order

#define INET_ADDRSTRLEN  16
#define INET6_ADDRSTRLEN 46

static inline uint16_t htons(uint16_t v)
{
    return __builtin_bswap16(v);
}

static inline uint16_t ntohs(uint16_t v)
{
    return __builtin_bswap16(v);
}

static inline uint32_t htonl(uint32_t v)
{
    return __builtin_bswap32(v);
}

static inline uint32_t ntohl(uint32_t v)
{
    return __builtin_bswap32(v);
}
