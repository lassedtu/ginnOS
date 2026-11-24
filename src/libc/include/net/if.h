#pragma once

#include <sys/socket.h>

/**
 * @file net/if.h
 * @brief network interfaces: struct ifreq for the SIOC ioctls of
 *        <sys/ioctl.h>, which work on any AF_INET socket.
 *
 * the layout is Linux's; it must match kifreq_t in
 * src/kernel/net/netif.h.
 */

#define IFNAMSIZ 16

#define IFF_UP        0x0001
#define IFF_BROADCAST 0x0002
#define IFF_LOOPBACK  0x0008
#define IFF_RUNNING   0x0040

struct ifreq
{
    char ifr_name[IFNAMSIZ];
    union
    {
        struct sockaddr ifru_addr;
        struct sockaddr ifru_netmask;
        struct sockaddr ifru_broadaddr;
        struct sockaddr ifru_hwaddr;
        short ifru_flags;
        int ifru_mtu;
        unsigned long __pad[3];
    } ifr_ifru;
};

#define ifr_addr      ifr_ifru.ifru_addr
#define ifr_netmask   ifr_ifru.ifru_netmask
#define ifr_broadaddr ifr_ifru.ifru_broadaddr
#define ifr_hwaddr    ifr_ifru.ifru_hwaddr
#define ifr_flags     ifr_ifru.ifru_flags
#define ifr_mtu       ifr_ifru.ifru_mtu
