/**
 * @file udp.c
 * @brief UDP (see udp.h).
 */

#include "udp.h"

#include "byteorder.h"
#include "checksum.h"
#include "common/memory.h"
#include "icmp.h"
#include "inet_socket.h"

void udp_receive(netif_t *netif, const ipv4_header_t *ip, const uint8_t *d, uint32_t length)
{
    (void)netif;
    if (length < UDP_HEADER_SIZE)
    {
        ipv4_stats.bad_header++;
        return;
    }
    uint32_t udp_length = (uint32_t)((d[4] << 8) | d[5]);
    if (udp_length < UDP_HEADER_SIZE || udp_length > length)
    {
        ipv4_stats.bad_header++;
        return;
    }
    // a checksum of 0 means the sender did not make one
    uint32_t pseudo = ipv4_pseudo_sum(ip->source, ip->destination, IP_PROTO_UDP, udp_length);
    if ((d[6] | d[7]) != 0 && checksum_finish(checksum_add(pseudo, d, udp_length)) != 0)
    {
        ipv4_stats.bad_header++;
        return;
    }
    uint16_t source_port = (uint16_t)((d[0] << 8) | d[1]);
    uint16_t dst_port = (uint16_t)((d[2] << 8) | d[3]);
    if (!inet_socket_deliver_udp(ip->destination, dst_port, ip->source, source_port,
                                 d + UDP_HEADER_SIZE, udp_length - UDP_HEADER_SIZE))
    {
        ipv4_stats.no_port++;
        icmp_send_unreachable(ip, ICMP_PORT_UNREACHABLE);
    }
}

bool udp_send(uint32_t source, uint16_t source_port, uint32_t dst, uint16_t dst_port,
              uint8_t *d, uint32_t length)
{
    if (source == 0)
    {
        source = ipv4_source_for(dst);
    }
    d[0] = (uint8_t)(source_port >> 8);
    d[1] = (uint8_t)source_port;
    d[2] = (uint8_t)(dst_port >> 8);
    d[3] = (uint8_t)dst_port;
    d[4] = (uint8_t)(length >> 8);
    d[5] = (uint8_t)length;
    d[6] = 0;
    d[7] = 0;
    uint32_t pseudo = ipv4_pseudo_sum(source, dst, IP_PROTO_UDP, length);
    uint16_t sum = checksum_finish(checksum_add(pseudo, d, length));
    if (sum == 0)
    {
        sum = 0xFFFF; // 0 would mean "no checksum"
    }
    memcpy(d + 6, &sum, 2);
    return ipv4_send(source, dst, IP_PROTO_UDP, d, length);
}
