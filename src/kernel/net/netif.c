/**
 * @file netif.c
 * @brief the interface table and the loopback interface (see netif.h).
 */

#include "netif.h"

#include "common/memory.h"
#include "common/string.h"
#include "kernel/sync/spinlock.h"

static spinlock_t lock; // zero: unlocked
static netif_t interfaces[NETIF_MAX];
static uint32_t interface_count;

uint32_t net_lock(void)
{
    return spin_lock_irqsave(&lock);
}

void net_unlock(uint32_t flags)
{
    spin_unlock_irqrestore(&lock, flags);
}

/**
 * the loopback interface gives each frame back as received.
 */
static bool loopback_send(netif_t *netif, const void *frame, uint32_t length)
{
    netif_receive(netif, frame, length);
    return true;
}

void net_init(void)
{
    netif_t *lo = netif_register("lo", NULL, 65535, loopback_send, NULL);
    if (lo)
    {
        lo->name[2] = '\0'; // "lo", not "lo0"
        lo->loopback = true;
        lo->address = 0x0100007F; // 127.0.0.1
        lo->netmask = 0x000000FF; // 255.0.0.0
    }
}

netif_t *netif_register(const char *prefix, const uint8_t mac[ETH_ADDR_LEN], uint32_t mtu,
                        netif_send_fn send, void *driver)
{
    uint32_t flags = net_lock();
    netif_t *netif = NULL;
    if (interface_count < NETIF_MAX)
    {
        // the number: how many interfaces already have this prefix
        uint32_t number = 0;
        size_t length = strlen(prefix);
        for (uint32_t i = 0; i < interface_count; i++)
        {
            if (strncmp(interfaces[i].name, prefix, length) == 0)
            {
                number++;
            }
        }
        netif = &interfaces[interface_count];
        memset(netif, 0, sizeof(*netif));
        size_t copy = length < NETIF_NAME_MAX - 2 ? length : NETIF_NAME_MAX - 2;
        memcpy(netif->name, prefix, copy);
        netif->name[copy] = (char)('0' + number % 10);
        if (mac)
        {
            memcpy(netif->mac, mac, ETH_ADDR_LEN);
        }
        netif->mtu = mtu;
        netif->send = send;
        netif->driver = driver;
        interface_count++;
    }
    net_unlock(flags);
    return netif;
}

void netif_receive(netif_t *netif, const void *frame, uint32_t length)
{
    (void)frame;
    netif->rx_packets++;
    netif->rx_bytes += length;
}

bool netif_send(netif_t *netif, const void *frame, uint32_t length)
{
    if (length > netif->mtu + 14 || !netif->send(netif, frame, length))
    {
        netif->tx_dropped++;
        return false;
    }
    netif->tx_packets++;
    netif->tx_bytes += length;
    return true;
}

netif_t *netif_at(uint32_t index)
{
    return index < interface_count ? &interfaces[index] : NULL;
}

netif_t *netif_find(const char *name)
{
    for (uint32_t i = 0; i < interface_count; i++)
    {
        if (strcmp(interfaces[i].name, name) == 0)
        {
            return &interfaces[i];
        }
    }
    return NULL;
}
