#pragma once

/**
 * @file netif.h
 * @brief network interfaces: the devices that send and receive frames.
 *
 * a driver registers an interface with a send function, and gives each
 * frame it receives to netif_receive. the stack is not ready yet (N0), so
 * a received frame is only counted.
 *
 * locking: all network state is under one spinlock (net_lock, irqsave).
 * frames are received in the device's interrupt handler, and the stack
 * handles them there, with the lock held. on one CPU that is simple and
 * safe: a syscall that sends holds the lock with interrupts off, so the
 * receive path cannot run at the same time.
 */

#include "common/stdint.h"

#define NETIF_MAX      4
#define NETIF_NAME_MAX 8
#define ETH_ADDR_LEN   6
#define ETH_MTU        1500

struct netif;

/**
 * send one frame (for Ethernet: from the destination address to the end
 * of the payload, no checksum). called with net_lock held.
 * @return false if the device has no room (the frame is dropped).
 */
typedef bool (*netif_send_fn)(struct netif *netif, const void *frame, uint32_t length);

typedef struct netif
{
    char name[NETIF_NAME_MAX];
    uint8_t mac[ETH_ADDR_LEN]; // all zero for the loopback interface
    uint32_t mtu;
    bool loopback;
    netif_send_fn send;
    void *driver; // the driver's own data

    // IPv4, in network byte order; 0 when not set
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;

    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint32_t rx_dropped;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint32_t tx_dropped;
} netif_t;

/**
 * set up the network layer and the loopback interface (lo).
 */
void net_init(void);

/**
 * take and release the network lock.
 */
uint32_t net_lock(void);
void net_unlock(uint32_t flags);

/**
 * add an interface. the name is made from prefix and a number (eth0).
 * @return the interface, or NULL if there are already NETIF_MAX.
 */
netif_t *netif_register(const char *prefix, const uint8_t mac[ETH_ADDR_LEN], uint32_t mtu,
                        netif_send_fn send, void *driver);

/**
 * a frame from the device (called with net_lock held, often from an
 * interrupt). the data is only valid during the call.
 */
void netif_receive(netif_t *netif, const void *frame, uint32_t length);

/**
 * send a frame on an interface and count it (net_lock held).
 */
bool netif_send(netif_t *netif, const void *frame, uint32_t length);

/**
 * the interfaces, for /proc/net and the configuration calls.
 * @return NULL after the last one.
 */
netif_t *netif_at(uint32_t index);

/**
 * the interface with this name, or NULL.
 */
netif_t *netif_find(const char *name);
