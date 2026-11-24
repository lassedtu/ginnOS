#pragma once

/**
 * @file ipv4.h
 * @brief IPv4 (RFC 791): the header, the checks, and where a packet goes.
 *
 * routing: 127.0.0.0/8 goes to the loopback interface; an address on the
 * local network of an interface goes straight to it; any other address
 * goes to the gateway of the first interface that has one. 255.255.255.255
 * goes to all hosts on the first Ethernet interface, also before it has an
 * address (for DHCP).
 *
 * fragments are not put together: a fragment is dropped and counted, and
 * every packet sent has the "don't fragment" bit set.
 */

#include "netif.h"

#define IPV4_HEADER_SIZE 20
#define IPV4_BROADCAST   0xFFFFFFFFu
#define IPV4_PACKET_MAX  65535

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

typedef struct
{
    uint8_t version_ihl; // 4 bits version, 4 bits header length in words
    uint8_t tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t fragment; // flags and the fragment offset
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t source;
    uint32_t destination;
} __attribute__((packed)) ipv4_header_t;

// counters for the whole stack
typedef struct
{
    uint32_t received;
    uint32_t delivered;
    uint32_t bad_header; // wrong version, length or checksum
    uint32_t fragments;  // dropped: fragments are not put together
    uint32_t not_for_us;
    uint32_t unknown_protocol;
    uint32_t no_port; // UDP: no socket for the port
    uint32_t sent;
    uint32_t no_route;
    uint32_t icmp_echo_answered;
    uint32_t icmp_echo_replies;
} ipv4_stats_t;

extern ipv4_stats_t ipv4_stats;

/**
 * get the send buffer (from net_init).
 * @return false if there is no memory.
 */
bool ipv4_init(void);

/**
 * a packet from an interface (net_lock held).
 */
void ipv4_receive(netif_t *netif, const uint8_t *packet, uint32_t length);

/**
 * send a payload to dst (net_lock held).
 * @param source the source address, or 0 for the address of the
 *        interface the packet goes out on.
 * @return false if there is no route or the device has no room.
 */
bool ipv4_send(uint32_t source, uint32_t dst, uint8_t protocol, const void *payload,
               uint32_t length);

/**
 * the source address a packet to dst gets (net_lock held), 0 if there is
 * no route.
 */
uint32_t ipv4_source_for(uint32_t dst);

/**
 * the interface a packet to dst goes out on, and the host it goes to
 * first (dst, or a gateway). net_lock held.
 * @return NULL if there is no route.
 */
netif_t *ipv4_route(uint32_t dst, uint32_t *next_hop);
