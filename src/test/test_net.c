/**
 * @file test_net.c
 * @brief the network stack: interfaces, the checksum, IPv4, ICMP, ARP.
 */

#include "ktest.h"

#include "common/memory.h"
#include "kernel/memory/heap.h"
#include "kernel/net/arp.h"
#include "kernel/net/byteorder.h"
#include "kernel/net/checksum.h"
#include "kernel/net/ethernet.h"
#include "kernel/net/icmp.h"
#include "kernel/net/ipv4.h"
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
    uint32_t length = 65535 + 15;
    uint8_t *frame = kcalloc(1, length);
    KEXPECT(frame != NULL);
    uint32_t flags = net_lock();
    bool ok = netif_send(lo, frame, length);
    net_unlock(flags);
    kfree(frame);
    KEXPECT(!ok);
    KEXPECT_EQ(lo->tx_dropped, dropped + 1);
}

KTEST(net_checksum_of_the_rfc_1071_example)
{
    const uint8_t data[8] = {0x00, 0x01, 0xF2, 0x03, 0xF4, 0xF5, 0xF6, 0xF7};
    uint16_t sum = checksum(data, sizeof(data));
    const uint8_t *bytes = (const uint8_t *)&sum;
    KEXPECT_EQ(bytes[0], 0x22);
    KEXPECT_EQ(bytes[1], 0x0D);
    // odd lengths pad with a zero byte
    const uint8_t odd[3] = {0x01, 0x02, 0x03};
    const uint8_t even[4] = {0x01, 0x02, 0x03, 0x00};
    KEXPECT_EQ(checksum(odd, 3), checksum(even, 4));
}

/**
 * an echo request with an identifier, a sequence number and 8 bytes of data.
 */
static uint32_t make_echo(uint8_t *m)
{
    memset(m, 0, 16);
    m[0] = ICMP_ECHO_REQUEST;
    m[5] = 7; // identifier 7
    m[7] = 1; // sequence 1
    memcpy(m + 8, "ginnOS!!", 8);
    uint16_t sum = checksum(m, 16);
    memcpy(m + 2, &sum, 2);
    return 16;
}

KTEST(net_ping_over_loopback_is_answered)
{
    uint8_t m[16];
    uint32_t length = make_echo(m);
    ipv4_stats_t before = ipv4_stats;
    uint32_t flags = net_lock();
    bool ok = ipv4_send(0, NET_IPV4(127, 0, 0, 1), IP_PROTO_ICMP, m, length);
    net_unlock(flags);
    KEXPECT(ok);
    KEXPECT_EQ(ipv4_stats.icmp_echo_answered, before.icmp_echo_answered + 1);
    KEXPECT_EQ(ipv4_stats.icmp_echo_replies, before.icmp_echo_replies + 1);
}

/**
 * a whole IPv4 packet around an echo request, sent on lo as it is.
 */
static uint32_t make_packet(uint8_t *p, uint16_t fragment)
{
    ipv4_header_t *h = (ipv4_header_t *)p;
    uint32_t length = make_echo(p + IPV4_HEADER_SIZE);
    memset(h, 0, IPV4_HEADER_SIZE);
    h->version_ihl = 0x45;
    h->total_length = net_htons((uint16_t)(IPV4_HEADER_SIZE + length));
    h->fragment = net_htons(fragment);
    h->ttl = 64;
    h->protocol = IP_PROTO_ICMP;
    h->source = NET_IPV4(127, 0, 0, 1);
    h->destination = NET_IPV4(127, 0, 0, 1);
    h->checksum = checksum(h, IPV4_HEADER_SIZE);
    return IPV4_HEADER_SIZE + length;
}

KTEST(net_bad_checksum_and_fragments_are_dropped)
{
    netif_t *lo = netif_find("lo");
    uint8_t p[64];
    ipv4_stats_t before = ipv4_stats;

    uint32_t length = make_packet(p, 0);
    p[8] ^= 1; // the TTL, so the header checksum is wrong
    uint32_t flags = net_lock();
    netif_send(lo, p, length);
    net_unlock(flags);
    KEXPECT_EQ(ipv4_stats.bad_header, before.bad_header + 1);

    length = make_packet(p, 0x2000); // more fragments follow
    flags = net_lock();
    netif_send(lo, p, length);
    net_unlock(flags);
    KEXPECT_EQ(ipv4_stats.fragments, before.fragments + 1);
    KEXPECT_EQ(ipv4_stats.icmp_echo_answered, before.icmp_echo_answered);
}

// a test interface that keeps the last frame it was given
static uint8_t last_frame[ETH_HEADER_SIZE + ETH_MTU];
static uint32_t last_length;
static uint32_t frames_sent;

static bool test_send(netif_t *netif, const void *frame, uint32_t length)
{
    (void)netif;
    memcpy(last_frame, frame, length);
    last_length = length;
    frames_sent++;
    return true;
}

static netif_t *test_interface(void)
{
    static netif_t *n;
    if (!n)
    {
        static const uint8_t mac[ETH_ADDR_LEN] = {0x02, 0, 0, 0, 0, 1};
        n = netif_register("test", mac, ETH_MTU, test_send, NULL);
        n->address = NET_IPV4(10, 99, 0, 1);
        n->netmask = NET_IPV4(255, 255, 255, 0);
    }
    return n;
}

KTEST(net_arp_resolves_then_sends_the_waiting_packet)
{
    netif_t *n = test_interface();
    KEXPECT(n != NULL);
    arp_flush();
    uint32_t peer = NET_IPV4(10, 99, 0, 5);
    static const uint8_t peer_mac[ETH_ADDR_LEN] = {0x02, 0, 0, 0, 0, 5};

    uint8_t m[16];
    uint32_t length = make_echo(m);
    uint32_t flags = net_lock();
    bool ok = ipv4_send(0, peer, IP_PROTO_ICMP, m, length);
    net_unlock(flags);
    KEXPECT(ok);
    // a request to all hosts: who has 10.99.0.5?
    KEXPECT_EQ(last_frame[12], 0x08);
    KEXPECT_EQ(last_frame[13], 0x06);
    KEXPECT(memcmp(last_frame, eth_broadcast, ETH_ADDR_LEN) == 0);
    KEXPECT_EQ(last_frame[ETH_HEADER_SIZE + 7], 1);
    KEXPECT(memcmp(last_frame + ETH_HEADER_SIZE + 24, &peer, 4) == 0);

    // the answer: the packet that waited goes to the peer
    uint8_t reply[ETH_FRAME_MIN] = {0};
    memcpy(reply, n->mac, ETH_ADDR_LEN);
    memcpy(reply + 6, peer_mac, ETH_ADDR_LEN);
    reply[12] = 0x08;
    reply[13] = 0x06;
    uint8_t *a = reply + ETH_HEADER_SIZE;
    a[1] = 1;
    a[2] = 0x08;
    a[4] = 6;
    a[5] = 4;
    a[7] = 2;
    memcpy(a + 8, peer_mac, ETH_ADDR_LEN);
    memcpy(a + 14, &peer, 4);
    memcpy(a + 18, n->mac, ETH_ADDR_LEN);
    memcpy(a + 24, &n->address, 4);
    uint32_t sent = frames_sent;
    flags = net_lock();
    netif_receive(n, reply, sizeof(reply));
    net_unlock(flags);
    KEXPECT_EQ(frames_sent, sent + 1);
    KEXPECT(memcmp(last_frame, peer_mac, ETH_ADDR_LEN) == 0);
    KEXPECT_EQ(last_frame[12], 0x08);
    KEXPECT_EQ(last_frame[13], 0x00);
    KEXPECT_EQ(last_frame[ETH_HEADER_SIZE + IPV4_HEADER_SIZE], ICMP_ECHO_REQUEST);

    // the next packet goes at once
    flags = net_lock();
    ipv4_send(0, peer, IP_PROTO_ICMP, m, length);
    net_unlock(flags);
    KEXPECT_EQ(frames_sent, sent + 2);
    KEXPECT(memcmp(last_frame, peer_mac, ETH_ADDR_LEN) == 0);
}

KTEST(net_arp_request_for_us_is_answered)
{
    netif_t *n = test_interface();
    uint32_t asker = NET_IPV4(10, 99, 0, 9);
    static const uint8_t asker_mac[ETH_ADDR_LEN] = {0x02, 0, 0, 0, 0, 9};
    uint8_t request[ETH_FRAME_MIN] = {0};
    memcpy(request, eth_broadcast, ETH_ADDR_LEN);
    memcpy(request + 6, asker_mac, ETH_ADDR_LEN);
    request[12] = 0x08;
    request[13] = 0x06;
    uint8_t *a = request + ETH_HEADER_SIZE;
    a[1] = 1;
    a[2] = 0x08;
    a[4] = 6;
    a[5] = 4;
    a[7] = 1;
    memcpy(a + 8, asker_mac, ETH_ADDR_LEN);
    memcpy(a + 14, &asker, 4);
    memcpy(a + 24, &n->address, 4);
    uint32_t flags = net_lock();
    netif_receive(n, request, sizeof(request));
    net_unlock(flags);
    // the reply goes to the asker and says our MAC address
    KEXPECT(memcmp(last_frame, asker_mac, ETH_ADDR_LEN) == 0);
    KEXPECT_EQ(last_frame[ETH_HEADER_SIZE + 7], 2);
    KEXPECT(memcmp(last_frame + ETH_HEADER_SIZE + 8, n->mac, ETH_ADDR_LEN) == 0);
}
