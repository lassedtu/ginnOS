#pragma once

/**
 * @file arp.h
 * @brief the Address Resolution Protocol (RFC 826): the Ethernet address
 *        of an IPv4 address on the local network.
 *
 * the cache keeps ARP_ENTRIES addresses for ARP_LIFETIME seconds. a packet
 * for an address not in the cache waits (one per address, the newest)
 * while a request goes out, and is sent when the answer comes.
 */

#include "netif.h"

#define ARP_ENTRIES  16
#define ARP_LIFETIME 300 // seconds

/**
 * an ARP packet from the network (net_lock held).
 */
void arp_receive(netif_t *netif, const uint8_t *packet, uint32_t length);

/**
 * send an IPv4 packet to the host ip on the local network of netif
 * (net_lock held). if its address is not known yet, the packet waits for
 * the answer to a request.
 * @return false if the packet cannot be sent or kept.
 */
bool arp_send_ipv4(netif_t *netif, uint32_t ip, const void *packet, uint32_t length);

/**
 * the Ethernet address of ip from the cache (net_lock held).
 * @return false if it is not there.
 */
bool arp_lookup(netif_t *netif, uint32_t ip, uint8_t mac[ETH_ADDR_LEN]);

/**
 * forget all entries (for the tests).
 */
void arp_flush(void);
