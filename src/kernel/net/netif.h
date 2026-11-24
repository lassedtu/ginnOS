#pragma once

/**
 * @file netif.h
 * @brief network interfaces: the devices that send and receive frames.
 *
 * a driver registers an interface with a send function, and gives each
 * frame it receives to netif_receive, which gives it to Ethernet (or, on
 * the loopback interface, which has no Ethernet header, to IPv4).
 *
 * locking: all network state is under one spinlock (net_lock, irqsave).
 * frames are received in the device's interrupt handler, and the stack
 * handles them there, with the lock held. on one CPU that is simple and
 * safe: a syscall that sends holds the lock with interrupts off, so the
 * receive path cannot run at the same time.
 */

#include "common/stdint.h"
#include "kernel/sync/wait_queue.h"

#define NETIF_MAX      4
#define NETIF_NAME_MAX 8
#define ETH_ADDR_LEN   6
#define ETH_MTU        1500

struct netif;

/**
 * send one frame (for Ethernet: from the destination address to the end
 * of the payload, no checksum; for loopback: an IPv4 packet). called with
 * net_lock held.
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
 * sleep on a wait queue with net_lock held (interruptibly): the lock is
 * let go while the process sleeps and held again on return. the caller
 * checks its condition and signal_pending in a loop.
 */
void net_wait(wait_queue_t *wq);

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

// ioctls on any IPv4 socket: read and set an interface (struct ifreq).
// the S calls need root. the numbers are Linux's, but for the gateway,
// which Linux sets through a routing table; ginnOS keeps one gateway per
// interface and uses two private numbers for it.
#define SIOCGIFFLAGS   0x8913
#define SIOCSIFFLAGS   0x8914
#define SIOCGIFADDR    0x8915
#define SIOCSIFADDR    0x8916
#define SIOCGIFBRDADDR 0x8919
#define SIOCGIFNETMASK 0x891B
#define SIOCSIFNETMASK 0x891C
#define SIOCGIFMTU     0x8921
#define SIOCGIFHWADDR  0x8927
#define SIOCGIFGATEWAY 0x89F0 // SIOCDEVPRIVATE: ifr_addr is the gateway
#define SIOCSIFGATEWAY 0x89F1

// ifr_flags
#define IFF_UP        0x0001
#define IFF_BROADCAST 0x0002
#define IFF_LOOPBACK  0x0008
#define IFF_RUNNING   0x0040

#define IFNAMSIZ 16

// struct ifreq: the name, then a union of 16 bytes on i686, 24 on x86_64
// (the size of Linux's struct ifmap there)
typedef struct
{
    char name[IFNAMSIZ];
    union
    {
        struct
        {
            uint16_t family;
            uint8_t data[14];
        } addr; // a sockaddr: an address, or the MAC with family 1 (ARPHRD_ETHER)
        int16_t flags;
        int32_t value; // the MTU
        unsigned long pad[3];
    };
} kifreq_t;

/**
 * an interface ioctl (from a socket).
 * @param arg a struct ifreq in user memory.
 * @return 0, or -ENODEV (no interface with the name), -EPERM, -EINVAL,
 *         -EFAULT, or -ENOTTY for another request.
 */
int32_t netif_ioctl(uint32_t request, void *arg);
