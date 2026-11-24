/**
 * @file netif.c
 * @brief the interface table and the loopback interface (see netif.h).
 */

#include "netif.h"

#include "common/memory.h"
#include "common/string.h"
#include "ethernet.h"
#include "ipv4.h"
#include "kernel/errno.h"
#include "kernel/memory/heap.h"
#include "kernel/process/process.h"
#include "kernel/usermode/uaccess.h"
#include "kernel/sync/spinlock.h"
#include "kernel/sync/wait_queue.h"

// packets on the loopback interface that wait to be received
#define LOOPBACK_QUEUE 16

static spinlock_t lock; // zero: unlocked
static netif_t interfaces[NETIF_MAX];
static uint32_t interface_count;

static struct
{
    uint8_t *data;
    uint32_t length;
} loopback_queue[LOOPBACK_QUEUE];
static uint32_t loopback_head;
static uint32_t loopback_count;
static bool loopback_draining;

uint32_t net_lock(void)
{
    return spin_lock_irqsave(&lock);
}

void net_unlock(uint32_t flags)
{
    spin_unlock_irqrestore(&lock, flags);
}

void net_wait(wait_queue_t *wq)
{
    wait_queue_block_locked_interruptible(wq, &lock);
}

/**
 * the loopback interface gives each packet back as received. a packet is
 * copied into a queue first: the stack can send again while it handles a
 * packet (an answer), and that send must not overwrite the packet. the
 * outermost send empties the queue.
 */
static bool loopback_send(netif_t *netif, const void *frame, uint32_t length)
{
    if (loopback_count == LOOPBACK_QUEUE)
    {
        return false;
    }
    uint8_t *copy = kmalloc(length ? length : 1);
    if (!copy)
    {
        return false;
    }
    memcpy(copy, frame, length);
    uint32_t tail = (loopback_head + loopback_count) % LOOPBACK_QUEUE;
    loopback_queue[tail].data = copy;
    loopback_queue[tail].length = length;
    loopback_count++;
    if (loopback_draining)
    {
        return true;
    }
    loopback_draining = true;
    while (loopback_count > 0)
    {
        uint8_t *data = loopback_queue[loopback_head].data;
        uint32_t size = loopback_queue[loopback_head].length;
        loopback_head = (loopback_head + 1) % LOOPBACK_QUEUE;
        loopback_count--;
        netif_receive(netif, data, size);
        kfree(data);
    }
    loopback_draining = false;
    return true;
}

void net_init(void)
{
    ipv4_init();
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
    netif->rx_packets++;
    netif->rx_bytes += length;
    if (netif->loopback)
    {
        ipv4_receive(netif, frame, length); // no Ethernet header on lo
    }
    else
    {
        eth_receive(netif, frame, length);
    }
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

#define AF_INET_K     2 // AF_INET (kernel/net/inet_socket.h)
#define ARPHRD_ETHER  1
#define ARPHRD_LOOPBACK 772

/**
 * an IPv4 address in a sockaddr_in in the request.
 */
static void put_address(kifreq_t *r, uint32_t address)
{
    memset(&r->addr, 0, sizeof(r->addr));
    r->addr.family = AF_INET_K;
    memcpy(r->addr.data + 2, &address, 4); // after sin_port
}

static bool get_address(const kifreq_t *r, uint32_t *address)
{
    if (r->addr.family != AF_INET_K)
    {
        return false;
    }
    memcpy(address, r->addr.data + 2, 4);
    return true;
}

/**
 * true if a netmask is ones then zeros (255.255.255.0, not 255.0.255.0).
 */
static bool netmask_ok(uint32_t mask)
{
    uint32_t host = ~__builtin_bswap32(mask); // the host part, in host order
    return (host & (host + 1)) == 0;
}

int32_t netif_ioctl(uint32_t request, void *arg)
{
    bool set = request == SIOCSIFADDR || request == SIOCSIFNETMASK ||
               request == SIOCSIFFLAGS || request == SIOCSIFGATEWAY;
    bool known = set || request == SIOCGIFADDR || request == SIOCGIFNETMASK ||
                 request == SIOCGIFFLAGS || request == SIOCGIFGATEWAY ||
                 request == SIOCGIFBRDADDR || request == SIOCGIFMTU || request == SIOCGIFHWADDR;
    if (!known)
    {
        return -ENOTTY;
    }
    kifreq_t r;
    if (kerr_failed(copy_from_user(&r, arg, sizeof(r))))
    {
        return -EFAULT;
    }
    r.name[IFNAMSIZ - 1] = '\0';
    if (set && process_current()->euid != 0)
    {
        return -EPERM;
    }

    int32_t result = 0;
    uint32_t flags = net_lock();
    netif_t *n = netif_find(r.name);
    uint32_t address;
    if (!n)
    {
        result = -ENODEV;
    }
    else
    {
        switch (request)
        {
        case SIOCGIFADDR:
            put_address(&r, n->address);
            break;
        case SIOCGIFNETMASK:
            put_address(&r, n->netmask);
            break;
        case SIOCGIFGATEWAY:
            put_address(&r, n->gateway);
            break;
        case SIOCGIFBRDADDR:
            put_address(&r, n->netmask ? (n->address | ~n->netmask) : 0);
            break;
        case SIOCGIFMTU:
            r.value = (int32_t)n->mtu;
            break;
        case SIOCGIFFLAGS:
            r.flags = (int16_t)(IFF_UP | IFF_RUNNING | (n->loopback ? IFF_LOOPBACK : IFF_BROADCAST));
            break;
        case SIOCGIFHWADDR:
            memset(&r.addr, 0, sizeof(r.addr));
            r.addr.family = n->loopback ? ARPHRD_LOOPBACK : ARPHRD_ETHER;
            memcpy(r.addr.data, n->mac, ETH_ADDR_LEN);
            break;
        case SIOCSIFFLAGS:
            break; // interfaces are always up
        case SIOCSIFADDR:
        case SIOCSIFNETMASK:
        case SIOCSIFGATEWAY:
            if (!get_address(&r, &address) || n->loopback ||
                (request == SIOCSIFNETMASK && !netmask_ok(address)))
            {
                result = -EINVAL;
            }
            else if (request == SIOCSIFADDR)
            {
                n->address = address;
            }
            else if (request == SIOCSIFNETMASK)
            {
                n->netmask = address;
            }
            else
            {
                n->gateway = address;
            }
            break;
        }
    }
    net_unlock(flags);
    if (result == 0 && !set)
    {
        result = kerr_to_errno(copy_to_user(arg, &r, sizeof(r)));
    }
    return result;
}
