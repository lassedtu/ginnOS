#pragma once

/**
 * @file netinet/tcp.h
 * @brief TCP socket options (setsockopt at IPPROTO_TCP).
 *
 * small writes are always sent at once, so TCP_NODELAY is always on. the
 * keepalive options are accepted and not used.
 */

#define TCP_NODELAY   1
#define TCP_MAXSEG    2
#define TCP_KEEPIDLE  4
#define TCP_KEEPINTVL 5
#define TCP_KEEPCNT   6
