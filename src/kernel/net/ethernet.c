/**
 * @file ethernet.c
 * @brief Ethernet II frames (see ethernet.h).
 */

#include "ethernet.h"

#include "arp.h"
#include "byteorder.h"
#include "common/memory.h"
#include "ipv4.h"

const uint8_t eth_broadcast[ETH_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// the frame being sent (net_lock held)
static uint8_t frame_out[ETH_HEADER_SIZE + ETH_MTU];

void eth_receive(netif_t *netif, const uint8_t *frame, uint32_t length)
{
    if (length < ETH_HEADER_SIZE)
    {
        netif->rx_dropped++;
        return;
    }
    bool to_us = memcmp(frame, netif->mac, ETH_ADDR_LEN) == 0;
    bool to_all = memcmp(frame, eth_broadcast, ETH_ADDR_LEN) == 0;
    if (!to_us && !to_all)
    {
        return; // for another host (or a multicast group we are not in)
    }
    uint16_t type = (uint16_t)((frame[12] << 8) | frame[13]);
    const uint8_t *payload = frame + ETH_HEADER_SIZE;
    uint32_t payload_length = length - ETH_HEADER_SIZE;
    if (type == ETH_TYPE_ARP)
    {
        arp_receive(netif, payload, payload_length);
    }
    else if (type == ETH_TYPE_IPV4)
    {
        ipv4_receive(netif, payload, payload_length);
    }
}

bool eth_send(netif_t *netif, const uint8_t dst[ETH_ADDR_LEN], uint16_t type,
              const void *payload, uint32_t length)
{
    if (length > ETH_MTU)
    {
        netif->tx_dropped++;
        return false;
    }
    memcpy(frame_out, dst, ETH_ADDR_LEN);
    memcpy(frame_out + 6, netif->mac, ETH_ADDR_LEN);
    frame_out[12] = (uint8_t)(type >> 8);
    frame_out[13] = (uint8_t)type;
    memcpy(frame_out + ETH_HEADER_SIZE, payload, length);
    uint32_t total = ETH_HEADER_SIZE + length;
    if (total < ETH_FRAME_MIN)
    {
        memset(frame_out + total, 0, ETH_FRAME_MIN - total);
        total = ETH_FRAME_MIN;
    }
    return netif_send(netif, frame_out, total);
}
