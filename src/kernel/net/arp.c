/**
 * @file arp.c
 * @brief the ARP cache (see arp.h).
 */

#include "arp.h"

#include "byteorder.h"
#include "common/memory.h"
#include "ethernet.h"
#include "kernel/memory/heap.h"
#include "kernel/time/timer.h"

#define PACKET_SIZE 28
#define OP_REQUEST  1
#define OP_REPLY    2

// a request again for a waiting packet after this many ticks
#define RETRY_TICKS (TIMER_HZ)

typedef struct
{
    netif_t *netif; // NULL: a free entry
    uint32_t ip;
    uint8_t mac[ETH_ADDR_LEN];
    bool resolved;
    uint64_t expires;   // resolved: when to forget it
    uint64_t asked;     // not resolved: when the last request went out
    uint8_t *waiting;   // a packet that waits for the answer (kmalloc)
    uint32_t waiting_length;
} entry_t;

static entry_t cache[ARP_ENTRIES];

static entry_t *find(netif_t *netif, uint32_t ip)
{
    for (int i = 0; i < ARP_ENTRIES; i++)
    {
        if (cache[i].netif == netif && cache[i].ip == ip)
        {
            return &cache[i];
        }
    }
    return NULL;
}

static void clear(entry_t *e)
{
    kfree(e->waiting);
    memset(e, 0, sizeof(*e));
}

/**
 * a free entry, else the one that expires first.
 */
static entry_t *take_entry(void)
{
    entry_t *oldest = &cache[0];
    uint64_t now = timer_now();
    for (int i = 0; i < ARP_ENTRIES; i++)
    {
        entry_t *e = &cache[i];
        if (!e->netif || (e->resolved && e->expires <= now))
        {
            clear(e);
            return e;
        }
        if (e->resolved && (!oldest->resolved || e->expires < oldest->expires))
        {
            oldest = e;
        }
    }
    clear(oldest);
    return oldest;
}

static void send_packet(netif_t *netif, uint16_t op, const uint8_t target_mac[ETH_ADDR_LEN],
                        uint32_t target_ip, const uint8_t dst[ETH_ADDR_LEN])
{
    uint8_t p[PACKET_SIZE];
    p[0] = 0;
    p[1] = 1; // Ethernet
    p[2] = ETH_TYPE_IPV4 >> 8;
    p[3] = ETH_TYPE_IPV4 & 0xFF;
    p[4] = ETH_ADDR_LEN;
    p[5] = 4;
    p[6] = 0;
    p[7] = (uint8_t)op;
    memcpy(p + 8, netif->mac, ETH_ADDR_LEN);
    memcpy(p + 14, &netif->address, 4);
    memcpy(p + 18, target_mac, ETH_ADDR_LEN);
    memcpy(p + 24, &target_ip, 4);
    eth_send(netif, dst, ETH_TYPE_ARP, p, sizeof(p));
}

static void request(netif_t *netif, uint32_t ip)
{
    static const uint8_t unknown[ETH_ADDR_LEN] = {0};
    send_packet(netif, OP_REQUEST, unknown, ip, eth_broadcast);
}

/**
 * keep the address of a host; send the packet that waited for it.
 */
static void learn(netif_t *netif, uint32_t ip, const uint8_t mac[ETH_ADDR_LEN], bool make)
{
    entry_t *e = find(netif, ip);
    if (!e)
    {
        if (!make)
        {
            return;
        }
        e = take_entry();
        e->netif = netif;
        e->ip = ip;
    }
    memcpy(e->mac, mac, ETH_ADDR_LEN);
    e->resolved = true;
    e->expires = timer_now() + (uint64_t)ARP_LIFETIME * TIMER_HZ;
    if (e->waiting)
    {
        uint8_t *packet = e->waiting;
        uint32_t length = e->waiting_length;
        e->waiting = NULL;
        eth_send(netif, mac, ETH_TYPE_IPV4, packet, length);
        kfree(packet);
    }
}

void arp_receive(netif_t *netif, const uint8_t *p, uint32_t length)
{
    if (length < PACKET_SIZE || p[0] != 0 || p[1] != 1 || p[2] != (ETH_TYPE_IPV4 >> 8) ||
        p[3] != (ETH_TYPE_IPV4 & 0xFF) || p[4] != ETH_ADDR_LEN || p[5] != 4)
    {
        netif->rx_dropped++;
        return;
    }
    uint16_t op = (uint16_t)((p[6] << 8) | p[7]);
    const uint8_t *sender_mac = p + 8;
    uint32_t sender_ip;
    uint32_t target_ip;
    memcpy(&sender_ip, p + 14, 4);
    memcpy(&target_ip, p + 24, 4);

    bool for_us = netif->address != 0 && target_ip == netif->address;
    if (sender_ip != 0)
    {
        // RFC 826: update an entry we have; add one when the packet is for us
        learn(netif, sender_ip, sender_mac, for_us);
    }
    if (for_us && op == OP_REQUEST)
    {
        send_packet(netif, OP_REPLY, sender_mac, sender_ip, sender_mac);
    }
}

bool arp_lookup(netif_t *netif, uint32_t ip, uint8_t mac[ETH_ADDR_LEN])
{
    entry_t *e = find(netif, ip);
    if (!e || !e->resolved || e->expires <= timer_now())
    {
        return false;
    }
    memcpy(mac, e->mac, ETH_ADDR_LEN);
    return true;
}

bool arp_send_ipv4(netif_t *netif, uint32_t ip, const void *packet, uint32_t length)
{
    uint8_t mac[ETH_ADDR_LEN];
    if (arp_lookup(netif, ip, mac))
    {
        return eth_send(netif, mac, ETH_TYPE_IPV4, packet, length);
    }

    entry_t *e = find(netif, ip);
    if (!e || e->resolved)
    {
        if (e)
        {
            clear(e); // expired
        }
        e = take_entry();
        e->netif = netif;
        e->ip = ip;
    }
    uint8_t *copy = kmalloc(length);
    if (!copy)
    {
        netif->tx_dropped++;
        return false;
    }
    memcpy(copy, packet, length);
    kfree(e->waiting); // the newest packet waits, the one before is dropped
    e->waiting = copy;
    e->waiting_length = length;

    uint64_t now = timer_now();
    if (e->asked == 0 || now - e->asked >= RETRY_TICKS)
    {
        e->asked = now ? now : 1;
        request(netif, ip);
    }
    return true;
}

void arp_flush(void)
{
    for (int i = 0; i < ARP_ENTRIES; i++)
    {
        clear(&cache[i]);
    }
}
