#pragma once

/**
 * @file inet_internal.h
 * @brief the parts of the IPv4 sockets that inet_socket.c (datagrams) and
 *        tcp_socket.c (streams) share. not for other code.
 */

#include "inet_socket.h"
#include "kernel/sync/wait_queue.h"
#include "tcp.h"

typedef struct datagram
{
    struct datagram *next;
    uint32_t from;
    uint16_t from_port;
    uint32_t length;
    uint8_t data[];
} datagram_t;

struct inet_socket
{
    inet_socket_t *next; // on the list of all sockets
    uint8_t protocol;    // IPPROTO_UDP, IPPROTO_ICMP or IPPROTO_TCP
    uint32_t local;      // network byte order, 0: any
    uint16_t port;       // host byte order, 0: not bound
    uint32_t remote;
    uint16_t remote_port;
    bool connected;
    bool reuse_address;
    bool broadcast;
    bool shut_read;
    bool shut_write;
    datagram_t *head;
    datagram_t *tail;
    uint32_t queued; // bytes in the queue
    wait_queue_t waiters;
    tcp_conn_t *conn; // TCP: the connection or the listener, or NULL
};

/**
 * make a socket and its file, on the list of all sockets.
 * @param flags O_NONBLOCK or 0.
 * @return 0, or -ENOMEM.
 */
int32_t inet_socket_alloc(uint8_t protocol, uint32_t flags, struct file **out,
                          inet_socket_t **socket);

/**
 * give a socket a free port (net_lock held).
 * @return false if all ports are taken.
 */
bool inet_bind_any_port(inet_socket_t *s);

/**
 * the TCP side of the socket calls (tcp_socket.c). those marked "lock
 * held" are called with net_lock held; the others take it.
 */
int32_t tcp_socket_connect(struct file *file, inet_socket_t *s, const ksockaddr_in_t *addr);
int32_t tcp_socket_listen(inet_socket_t *s, uint32_t backlog);
int32_t tcp_socket_accept(struct file *file, uint32_t flags, struct file **out,
                          ksockaddr_in_t *peer);
int32_t tcp_socket_send(struct file *file, inet_socket_t *s, const kiovec_t *iov,
                        uint32_t iov_count, bool dontwait);
int32_t tcp_socket_receive(struct file *file, inet_socket_t *s, const kiovec_t *iov,
                           uint32_t iov_count, uint32_t flags, bool dontwait);
uint32_t tcp_socket_poll(inet_socket_t *s);                // lock held
int32_t tcp_socket_shutdown(inet_socket_t *s, int32_t how); // lock held
