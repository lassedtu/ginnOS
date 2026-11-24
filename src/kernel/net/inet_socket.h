#pragma once

/**
 * @file inet_socket.h
 * @brief IPv4 sockets: UDP (SOCK_DGRAM) and ICMP echo (SOCK_DGRAM with
 *        IPPROTO_ICMP, for ping, as Linux's ping sockets).
 *
 * a datagram socket keeps the datagrams it receives in a queue, up to
 * INET_RECEIVE_MAX bytes; more are dropped. an ICMP socket sends an echo
 * request that the program writes (type, code, then the data), with the
 * identifier set to the socket's port and the checksum filled in, and it
 * receives the echo replies with that identifier.
 *
 * the layouts below are the user ABI (<netinet/in.h> and <sys/socket.h>
 * in libc); they are the same as Linux's.
 */

#include "common/error.h"
#include "common/stdint.h"
#include "kernel/ipc/unix_socket.h"

struct file;

#define AF_INET    2
#define SOCK_DGRAM 2

#define IPPROTO_IP   0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

// setsockopt and getsockopt at SOL_SOCKET
#define SO_REUSEADDR 2
#define SO_TYPE      3
#define SO_ERROR     4
#define SO_BROADCAST 6
#define SO_SNDBUF    7
#define SO_RCVBUF    8
#define SO_KEEPALIVE 9

#define FIONREAD 0x541B // ioctl: the bytes of the next datagram

#define MSG_PEEK  0x0002
#define MSG_TRUNC 0x0020

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#define INET_RECEIVE_MAX (64 * 1024)
#define INET_PORT_FIRST  49152 // the ports given to sockets that do not bind one

typedef struct
{
    uint16_t sin_family;
    uint16_t sin_port; // network byte order
    uint32_t sin_addr; // network byte order
    uint8_t sin_zero[8];
} ksockaddr_in_t;

typedef struct inet_socket inet_socket_t;

/**
 * make a new socket and its open file.
 * @param type SOCK_DGRAM.
 * @param protocol 0 or IPPROTO_UDP for UDP, IPPROTO_ICMP for ping.
 * @param flags O_NONBLOCK or 0.
 * @return 0, or -EPROTONOSUPPORT, -ENOMEM.
 */
int32_t inet_socket_create(uint32_t type, uint32_t protocol, uint32_t flags,
                           struct file **out);

/**
 * the socket of an open file, or NULL if the file is not an IPv4 socket.
 */
inet_socket_t *inet_socket_of(struct file *file);

/**
 * give a socket a local address and port (port 0: one not in use).
 * @return 0, or -EADDRINUSE, -EADDRNOTAVAIL, -EINVAL.
 */
int32_t inet_socket_bind(inet_socket_t *s, const ksockaddr_in_t *addr);

/**
 * set the address datagrams go to and the only one they come from.
 * AF_UNSPEC (family 0) takes the connection away.
 * @return 0, or -EAFNOSUPPORT, -ENETUNREACH.
 */
int32_t inet_socket_connect(inet_socket_t *s, const ksockaddr_in_t *addr);

/**
 * send one datagram from user memory.
 * @param to where it goes, or NULL for the connected address.
 * @return the bytes sent, or -EDESTADDRREQ, -EMSGSIZE, -ENETUNREACH,
 *         -EACCES (broadcast without SO_BROADCAST), -ENOBUFS, -ENOMEM,
 *         -EPIPE (shut down for writing), -EFAULT.
 */
int32_t inet_socket_send(struct file *file, const kiovec_t *iov, uint32_t iov_count,
                         const ksockaddr_in_t *to);

/**
 * receive one datagram into user memory (blocks unless the file is
 * non-blocking or dontwait). a datagram longer than the buffers is cut;
 * the rest of it is gone.
 * @param from receives the sender (may be NULL).
 * @param flags MSG_PEEK: leave it in the queue.
 * @param full receives the datagram's whole length (may be NULL).
 * @return the bytes received, or -EAGAIN, -ERESTARTSYS, -EFAULT; 0 when
 *         shut down for reading and the queue is empty.
 */
int32_t inet_socket_receive(struct file *file, const kiovec_t *iov, uint32_t iov_count,
                            ksockaddr_in_t *from, uint32_t flags, bool dontwait,
                            uint32_t *full);

/**
 * the local address (peer false) or the connected one (peer true).
 * @return 0, or -ENOTCONN.
 */
int32_t inet_socket_name(inet_socket_t *s, bool peer, ksockaddr_in_t *out);

/**
 * setsockopt and getsockopt (an int value).
 * @return 0, or -ENOPROTOOPT.
 */
int32_t inet_socket_set_option(inet_socket_t *s, int32_t level, int32_t name, int32_t value);
int32_t inet_socket_get_option(inet_socket_t *s, int32_t level, int32_t name, int32_t *value);

/**
 * shutdown: SHUT_RD, SHUT_WR or SHUT_RDWR.
 * @return 0, or -EINVAL.
 */
int32_t inet_socket_shutdown(inet_socket_t *s, int32_t how);

/**
 * give a received UDP datagram to the socket it is for (net_lock held).
 * @return false if no socket takes it.
 */
bool inet_socket_deliver_udp(uint32_t dst, uint16_t dst_port, uint32_t src, uint16_t src_port,
                             const uint8_t *data, uint32_t length);

/**
 * give a received echo reply to the ICMP socket with its identifier
 * (net_lock held).
 */
void inet_socket_deliver_echo(uint32_t src, uint16_t id, const uint8_t *message,
                              uint32_t length);
