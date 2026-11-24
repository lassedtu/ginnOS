#pragma once

#include <netinet/in.h>

/**
 * @file arpa/inet.h
 * @brief IPv4 addresses as text ("10.0.2.15") and back.
 */

/**
 * text to an address.
 * @param af AF_INET.
 * @param dst receives a struct in_addr.
 * @return 1, 0 if src is not an address, -1 (EAFNOSUPPORT) for another af.
 */
int inet_pton(int af, const char *src, void *dst);

/**
 * an address to text.
 * @param size at least INET_ADDRSTRLEN for AF_INET.
 * @return dst, or NULL (EAFNOSUPPORT, ENOSPC).
 */
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

/**
 * text to an address, as inet_aton does; the address, or INADDR_NONE.
 */
in_addr_t inet_addr(const char *text);

/**
 * text to an address. as well as a.b.c.d this takes the older forms: a.b.c
 * (c is 16 bits), a.b (b is 24 bits) and a (32 bits), each part in
 * decimal, octal (0) or hex (0x).
 * @return 1, or 0 if text is not an address.
 */
int inet_aton(const char *text, struct in_addr *out);

/**
 * an address to text, in a buffer of the calling thread.
 */
char *inet_ntoa(struct in_addr in);
