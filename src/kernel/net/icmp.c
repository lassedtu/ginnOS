/**
 * @file icmp.c
 * @brief ICMP (see icmp.h).
 */

#include "icmp.h"

#include "byteorder.h"
#include "checksum.h"
#include "common/memory.h"
#include "inet_socket.h"

#define HEADER_SIZE 8

// the message being sent (net_lock held)
static uint8_t message_out[ETH_MTU];

void icmp_receive(netif_t *netif, const ipv4_header_t *ip, const uint8_t *message,
                  uint32_t length)
{
    (void)netif;
    if (length < HEADER_SIZE || checksum(message, length) != 0)
    {
        ipv4_stats.bad_header++;
        return;
    }
    uint8_t type = message[0];
    if (type == ICMP_ECHO_REQUEST && ip->destination != IPV4_BROADCAST &&
        length <= sizeof(message_out))
    {
        // the same identifier, sequence and data back
        memcpy(message_out, message, length);
        message_out[0] = ICMP_ECHO_REPLY;
        message_out[1] = 0;
        message_out[2] = 0;
        message_out[3] = 0;
        uint16_t sum = checksum(message_out, length);
        memcpy(message_out + 2, &sum, 2);
        if (ipv4_send(ip->destination, ip->source, IP_PROTO_ICMP, message_out, length))
        {
            ipv4_stats.icmp_echo_answered++;
        }
    }
    else if (type == ICMP_ECHO_REPLY)
    {
        ipv4_stats.icmp_echo_replies++;
        uint16_t id = (uint16_t)((message[4] << 8) | message[5]);
        inet_socket_deliver_echo(ip->source, id, message, length);
    }
}

void icmp_send_unreachable(const ipv4_header_t *ip, uint8_t code)
{
    if (ip->destination == IPV4_BROADCAST || ip->protocol == IP_PROTO_ICMP)
    {
        return;
    }
    // the header and the first 8 bytes of the packet that was not delivered
    uint32_t header_length = (uint32_t)(ip->version_ihl & 0x0F) * 4;
    uint32_t total = net_ntohs(ip->total_length);
    uint32_t quoted = header_length + 8 < total ? header_length + 8 : total;
    memset(message_out, 0, HEADER_SIZE);
    message_out[0] = ICMP_UNREACHABLE;
    message_out[1] = code;
    memcpy(message_out + HEADER_SIZE, ip, quoted);
    uint16_t sum = checksum(message_out, HEADER_SIZE + quoted);
    memcpy(message_out + 2, &sum, 2);
    ipv4_send(0, ip->source, IP_PROTO_ICMP, message_out, HEADER_SIZE + quoted);
}
