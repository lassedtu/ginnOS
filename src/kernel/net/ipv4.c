/**
 * @file ipv4.c
 * @brief IPv4 (see ipv4.h).
 */

#include "ipv4.h"

#include "arp.h"
#include "byteorder.h"
#include "checksum.h"
#include "common/memory.h"
#include "ethernet.h"
#include "icmp.h"
#include "kernel/memory/heap.h"

#define VERSION_IHL  0x45 // version 4, five words of header
#define DEFAULT_TTL  64
#define FLAG_DF      0x4000
#define FLAG_MF      0x2000
#define OFFSET_MASK  0x1FFF
#define LOOPBACK_NET 0x7F // 127.0.0.0/8: the first byte

ipv4_stats_t ipv4_stats;

// the packet being sent (net_lock held), IPV4_PACKET_MAX bytes from the
// heap (the kernel image must stay small)
static uint8_t *packet_out;
static uint16_t next_id = 1;

static bool is_local(const netif_t *netif, uint32_t dst)
{
    if (dst == IPV4_BROADCAST || dst == netif->address || netif->address == 0)
    {
        return true; // without an address, take everything (DHCP)
    }
    // the broadcast address of the network
    return netif->netmask != 0 && dst == (netif->address | ~netif->netmask);
}

void ipv4_receive(netif_t *netif, const uint8_t *packet, uint32_t length)
{
    ipv4_stats.received++;
    if (length < IPV4_HEADER_SIZE)
    {
        ipv4_stats.bad_header++;
        return;
    }
    const ipv4_header_t *h = (const ipv4_header_t *)packet;
    uint32_t header_length = (uint32_t)(h->version_ihl & 0x0F) * 4;
    uint32_t total = net_ntohs(h->total_length);
    if ((h->version_ihl >> 4) != 4 || header_length < IPV4_HEADER_SIZE || total < header_length ||
        total > length || checksum(packet, header_length) != 0)
    {
        ipv4_stats.bad_header++;
        return;
    }
    uint16_t fragment = net_ntohs(h->fragment);
    if ((fragment & FLAG_MF) || (fragment & OFFSET_MASK))
    {
        ipv4_stats.fragments++;
        return;
    }
    if (!netif->loopback && !is_local(netif, h->destination))
    {
        ipv4_stats.not_for_us++;
        return;
    }

    const uint8_t *payload = packet + header_length;
    uint32_t payload_length = total - header_length;
    switch (h->protocol)
    {
    case IP_PROTO_ICMP:
        ipv4_stats.delivered++;
        icmp_receive(netif, h, payload, payload_length);
        break;
    default:
        ipv4_stats.unknown_protocol++;
        icmp_send_unreachable(h, ICMP_PROTOCOL_UNREACHABLE);
        break;
    }
}

netif_t *ipv4_route(uint32_t dst, uint32_t *next_hop)
{
    *next_hop = dst;
    netif_t *n;
    if ((dst & 0xFF) == LOOPBACK_NET)
    {
        return netif_find("lo");
    }
    netif_t *first_ethernet = NULL;
    for (uint32_t i = 0; (n = netif_at(i)) != NULL; i++)
    {
        if (n->loopback)
        {
            if (dst == n->address)
            {
                return n;
            }
            continue;
        }
        if (!first_ethernet)
        {
            first_ethernet = n;
        }
        if (n->address != 0 && dst == n->address)
        {
            return netif_find("lo"); // to ourselves
        }
        if (n->address != 0 && n->netmask != 0 &&
            (dst & n->netmask) == (n->address & n->netmask))
        {
            return n;
        }
    }
    if (dst == IPV4_BROADCAST)
    {
        return first_ethernet;
    }
    for (uint32_t i = 0; (n = netif_at(i)) != NULL; i++)
    {
        if (!n->loopback && n->gateway != 0)
        {
            *next_hop = n->gateway;
            return n;
        }
    }
    return NULL;
}

bool ipv4_init(void)
{
    packet_out = kmalloc(IPV4_PACKET_MAX);
    return packet_out != NULL;
}

bool ipv4_send(uint32_t source, uint32_t dst, uint8_t protocol, const void *payload,
               uint32_t length)
{
    uint32_t next_hop;
    netif_t *netif = ipv4_route(dst, &next_hop);
    if (!netif || !packet_out || length > netif->mtu - IPV4_HEADER_SIZE ||
        length > IPV4_PACKET_MAX - IPV4_HEADER_SIZE)
    {
        ipv4_stats.no_route++;
        return false;
    }
    if (source == 0)
    {
        source = netif->loopback && (dst & 0xFF) != LOOPBACK_NET ? dst : netif->address;
    }

    ipv4_header_t *h = (ipv4_header_t *)packet_out;
    h->version_ihl = VERSION_IHL;
    h->tos = 0;
    h->total_length = net_htons((uint16_t)(IPV4_HEADER_SIZE + length));
    h->id = net_htons(next_id++);
    h->fragment = net_htons(FLAG_DF);
    h->ttl = DEFAULT_TTL;
    h->protocol = protocol;
    h->checksum = 0;
    h->source = source;
    h->destination = dst;
    h->checksum = checksum(h, IPV4_HEADER_SIZE);
    memcpy(packet_out + IPV4_HEADER_SIZE, payload, length);
    uint32_t total = IPV4_HEADER_SIZE + length;

    ipv4_stats.sent++;
    if (netif->loopback)
    {
        return netif_send(netif, packet_out, total);
    }
    if (dst == IPV4_BROADCAST || (netif->netmask != 0 && dst == (netif->address | ~netif->netmask)))
    {
        return eth_send(netif, eth_broadcast, ETH_TYPE_IPV4, packet_out, total);
    }
    return arp_send_ipv4(netif, next_hop, packet_out, total);
}
