/**
 * @file test_net.c
 * @brief the network interfaces: the loopback interface and the counters.
 */

#include "ktest.h"

#include "kernel/net/netif.h"

KTEST(net_loopback_is_there)
{
    netif_t *lo = netif_find("lo");
    KEXPECT(lo != NULL);
    KEXPECT(lo->loopback);
    KEXPECT_EQ(lo->address, 0x0100007Fu);
}

KTEST(net_loopback_gives_frames_back)
{
    netif_t *lo = netif_find("lo");
    KEXPECT(lo != NULL);
    uint64_t received = lo->rx_packets;
    uint64_t sent = lo->tx_packets;
    uint8_t frame[64] = {0};
    uint32_t flags = net_lock();
    bool ok = netif_send(lo, frame, sizeof(frame));
    net_unlock(flags);
    KEXPECT(ok);
    KEXPECT_EQ(lo->tx_packets, sent + 1);
    KEXPECT_EQ(lo->rx_packets, received + 1);
}

KTEST(net_frame_above_the_mtu_is_dropped)
{
    netif_t *lo = netif_find("lo");
    KEXPECT(lo != NULL);
    uint32_t dropped = lo->tx_dropped;
    static uint8_t frame[65535 + 15];
    uint32_t flags = net_lock();
    bool ok = netif_send(lo, frame, sizeof(frame));
    net_unlock(flags);
    KEXPECT(!ok);
    KEXPECT_EQ(lo->tx_dropped, dropped + 1);
}
