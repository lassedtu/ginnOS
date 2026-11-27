#pragma once

/**
 * @file icmp.h
 * @brief ICMP for IPv4 (RFC 792): echo (ping) and "unreachable".
 */

#include "ipv4.h"

#define ICMP_ECHO_REPLY   0
#define ICMP_UNREACHABLE  3
#define ICMP_ECHO_REQUEST 8

// codes of ICMP_UNREACHABLE
#define ICMP_PROTOCOL_UNREACHABLE 2
#define ICMP_PORT_UNREACHABLE     3

/**
 * an ICMP message (net_lock held): answers an echo request.
 */
void icmp_receive(netif_t *netif, const ipv4_header_t *ip, const uint8_t *message,
                  uint32_t length);

/**
 * tell the sender of a packet that it cannot be delivered (net_lock
 * held). nothing is sent for a broadcast or an ICMP error.
 */
void icmp_send_unreachable(const ipv4_header_t *ip, uint8_t code);
