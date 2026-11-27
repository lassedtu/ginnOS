#pragma once

/**
 * @file dns.h
 * @brief the libc's DNS client (dns.c), for getaddrinfo (netdb.c).
 */

#include <stddef.h>
#include <stdint.h>

#define DNS_MAX_ADDRESSES 8
#define DNS_NAME_MAX      256

/**
 * the IPv4 addresses (A records) of a name, from the name servers in
 * /etc/resolv.conf.
 * @param addresses receives up to max addresses (network byte order).
 * @param canonical receives the canonical name (the end of the CNAME
 *        chain), at least DNS_NAME_MAX bytes.
 * @return the number of addresses (1 or more), or EAI_NONAME, EAI_AGAIN,
 *         EAI_FAIL.
 */
int __dns_lookup(const char *name, uint32_t *addresses, int max, char *canonical);
