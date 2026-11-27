#pragma once

/**
 * @file udp.h
 * @brief UDP (RFC 768): datagrams between ports.
 */

#include "ipv4.h"

#define UDP_HEADER_SIZE 8

/**
 * a received UDP datagram (net_lock held): checks it and gives it to the
 * socket for its port, or answers "port unreachable".
 */
void udp_receive(netif_t *netif, const ipv4_header_t *ip, const uint8_t *datagram,
                 uint32_t length);

/**
 * send a datagram (net_lock held). the first UDP_HEADER_SIZE bytes of
 * datagram are the room for the header, which this fills in.
 * @param source the source address, or 0 for the address of the
 *        interface the datagram goes out on.
 * @param length the length with the header.
 */
bool udp_send(uint32_t source, uint16_t source_port, uint32_t dst, uint16_t dst_port,
              uint8_t *datagram, uint32_t length);
