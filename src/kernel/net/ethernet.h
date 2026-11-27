#pragma once

/**
 * @file ethernet.h
 * @brief Ethernet II frames: the 14-byte header in front of ARP and IPv4.
 */

#include "netif.h"

#define ETH_HEADER_SIZE 14
#define ETH_FRAME_MIN   60 // without the checksum the card adds
#define ETH_TYPE_IPV4   0x0800
#define ETH_TYPE_ARP    0x0806

extern const uint8_t eth_broadcast[ETH_ADDR_LEN];

/**
 * a received frame (net_lock held): checks the address and gives the
 * payload to ARP or IPv4.
 */
void eth_receive(netif_t *netif, const uint8_t *frame, uint32_t length);

/**
 * send a payload in a frame to dst (net_lock held). short frames are
 * padded to ETH_FRAME_MIN.
 */
bool eth_send(netif_t *netif, const uint8_t dst[ETH_ADDR_LEN], uint16_t type,
              const void *payload, uint32_t length);
