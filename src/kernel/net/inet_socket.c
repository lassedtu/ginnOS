/**
 * @file inet_socket.c
 * @brief IPv4 sockets: UDP and ICMP echo here, and the calls of TCP
 *        sockets go to tcp_socket.c (see inet_socket.h).
 *
 * all sockets are on one list, under net_lock, so a received datagram
 * finds its socket there. data from user memory is copied into the
 * kernel before the lock is taken, and to user memory after it is let go.
 */

#include "inet_internal.h"

#include "byteorder.h"
#include "checksum.h"
#include "common/memory.h"
#include "icmp.h"
#include "ipv4.h"
#include "kernel/errno.h"
#include "kernel/memory/heap.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"
#include "kernel/sync/wait_queue.h"
#include "kernel/usermode/uaccess.h"
#include "kernel/vfs/file.h"
#include "kernel/vfs/poll.h"
#include "kernel/vfs/stat.h"
#include "netif.h"
#include "udp.h"

#define ICMP_HEADER_SIZE 8
#define DATAGRAM_MAX     65507 // the most UDP data in one IPv4 packet

static inet_socket_t *sockets;
static uint16_t next_port = INET_PORT_FIRST;
static const file_ops_t inet_file_ops;

inet_socket_t *inet_socket_of(file_t *file)
{
    return file && file->ops == &inet_file_ops ? file->device_data : NULL;
}

int32_t inet_socket_create(uint32_t type, uint32_t protocol, uint32_t flags, file_t **out)
{
    if (type == SOCK_STREAM)
    {
        if (protocol != IPPROTO_IP && protocol != IPPROTO_TCP)
        {
            return -EPROTONOSUPPORT;
        }
        protocol = IPPROTO_TCP;
    }
    else if (type == SOCK_DGRAM)
    {
        if (protocol == IPPROTO_IP)
        {
            protocol = IPPROTO_UDP;
        }
        if (protocol != IPPROTO_UDP && protocol != IPPROTO_ICMP)
        {
            return -EPROTONOSUPPORT;
        }
    }
    else
    {
        return -EPROTONOSUPPORT;
    }
    inet_socket_t *s;
    return inet_socket_alloc((uint8_t)protocol, flags, out, &s);
}

int32_t inet_socket_alloc(uint8_t protocol, uint32_t flags, file_t **out, inet_socket_t **socket)
{
    inet_socket_t *s = kcalloc(1, sizeof(*s));
    if (!s)
    {
        return -ENOMEM;
    }
    file_t *file = file_new(&inet_file_ops, O_RDWR | (flags & O_NONBLOCK));
    if (!file)
    {
        kfree(s);
        return -ENOMEM;
    }
    s->protocol = protocol;
    wait_queue_init(&s->waiters);
    file->device_data = s;

    uint32_t lock = net_lock();
    s->next = sockets;
    sockets = s;
    net_unlock(lock);
    *out = file;
    *socket = s;
    return 0;
}

/**
 * true if an address is one of ours (or one any socket may bind to).
 */
static bool address_is_ours(uint32_t addr)
{
    if (addr == 0 || addr == IPV4_BROADCAST || (addr & 0xFF) == 0x7F)
    {
        return true;
    }
    netif_t *n;
    for (uint32_t i = 0; (n = netif_at(i)) != NULL; i++)
    {
        if (n->address == addr)
        {
            return true;
        }
    }
    return false;
}

/**
 * true if another socket has the port on an address that overlaps
 * (net_lock held).
 */
static bool port_in_use(const inet_socket_t *self, uint8_t protocol, uint32_t local,
                        uint16_t port, bool reuse)
{
    for (inet_socket_t *o = sockets; o; o = o->next)
    {
        if (o == self || o->protocol != protocol || o->port != port)
        {
            continue;
        }
        bool overlap = o->local == 0 || local == 0 || o->local == local;
        if (overlap && !(reuse && o->reuse_address))
        {
            return true;
        }
    }
    return false;
}

bool inet_bind_any_port(inet_socket_t *s)
{
    for (uint32_t tries = 0; tries < 65536 - INET_PORT_FIRST; tries++)
    {
        uint16_t port = next_port;
        next_port = next_port == 65535 ? INET_PORT_FIRST : (uint16_t)(next_port + 1);
        if (!port_in_use(s, s->protocol, s->local, port, false))
        {
            s->port = port;
            return true;
        }
    }
    return false;
}

int32_t inet_socket_bind(inet_socket_t *s, const ksockaddr_in_t *addr)
{
    if (addr->sin_family != AF_INET)
    {
        return -EAFNOSUPPORT;
    }
    int32_t result = 0;
    uint32_t lock = net_lock();
    uint16_t port = net_ntohs(addr->sin_port);
    if (s->port != 0)
    {
        result = -EINVAL; // bound already
    }
    else if (!address_is_ours(addr->sin_addr))
    {
        result = -EADDRNOTAVAIL;
    }
    else if (port != 0 && port_in_use(s, s->protocol, addr->sin_addr, port, s->reuse_address))
    {
        result = -EADDRINUSE;
    }
    else
    {
        s->local = addr->sin_addr;
        if (port != 0)
        {
            s->port = port;
        }
        else if (!inet_bind_any_port(s))
        {
            s->local = 0;
            result = -EADDRINUSE;
        }
    }
    net_unlock(lock);
    return result;
}

int32_t inet_socket_connect(file_t *file, const ksockaddr_in_t *addr)
{
    inet_socket_t *s = inet_socket_of(file);
    if (s->protocol == IPPROTO_TCP)
    {
        return tcp_socket_connect(file, s, addr);
    }
    int32_t result = 0;
    uint32_t lock = net_lock();
    if (addr->sin_family == 0)
    {
        s->connected = false; // AF_UNSPEC: forget the peer
    }
    else if (addr->sin_family != AF_INET)
    {
        result = -EAFNOSUPPORT;
    }
    else if (ipv4_source_for(addr->sin_addr) == 0 && addr->sin_addr != IPV4_BROADCAST)
    {
        result = -ENETUNREACH;
    }
    else if (s->port == 0 && !inet_bind_any_port(s))
    {
        result = -EADDRINUSE;
    }
    else
    {
        // (a datagram socket only sets the peer)
        s->remote = addr->sin_addr;
        s->remote_port = net_ntohs(addr->sin_port);
        s->connected = true;
    }
    net_unlock(lock);
    return result;
}

/**
 * true if dst is a broadcast address (all hosts, or a local network's).
 */
static bool is_broadcast(uint32_t dst)
{
    if (dst == IPV4_BROADCAST)
    {
        return true;
    }
    netif_t *n;
    for (uint32_t i = 0; (n = netif_at(i)) != NULL; i++)
    {
        if (!n->loopback && n->netmask != 0 && dst == (n->address | ~n->netmask))
        {
            return true;
        }
    }
    return false;
}

/**
 * copy the iovecs from user memory into buffer.
 */
static int32_t gather(uint8_t *buffer, const kiovec_t *iov, uint32_t iov_count)
{
    uint32_t at = 0;
    for (uint32_t i = 0; i < iov_count; i++)
    {
        if (iov[i].len > 0 &&
            kerr_failed(copy_from_user(buffer + at, (const void *)iov[i].base, iov[i].len)))
        {
            return -EFAULT;
        }
        at += (uint32_t)iov[i].len;
    }
    return 0;
}

int32_t inet_socket_send(file_t *file, const kiovec_t *iov, uint32_t iov_count,
                         const ksockaddr_in_t *to, uint32_t flags)
{
    inet_socket_t *s = inet_socket_of(file);
    if (!s)
    {
        return -ENOTSOCK;
    }
    if (s->protocol == IPPROTO_TCP)
    {
        // a stream is connected: the address is not used
        return tcp_socket_send(file, s, iov, iov_count, (flags & MSG_DONTWAIT) != 0);
    }
    if (to && to->sin_family != AF_INET)
    {
        return -EAFNOSUPPORT;
    }
    size_t total = 0;
    for (uint32_t i = 0; i < iov_count; i++)
    {
        total += iov[i].len;
    }
    if (total > DATAGRAM_MAX)
    {
        return -EMSGSIZE;
    }
    bool icmp = s->protocol == IPPROTO_ICMP;
    if (icmp && total < ICMP_HEADER_SIZE)
    {
        return -EINVAL;
    }

    // UDP: room for the header in front of the data
    uint32_t header = icmp ? 0 : UDP_HEADER_SIZE;
    uint8_t *buffer = kmalloc(header + total + 1);
    if (!buffer)
    {
        return -ENOMEM;
    }
    int32_t result = gather(buffer + header, iov, iov_count);
    if (result != 0)
    {
        kfree(buffer);
        return result;
    }

    uint32_t lock = net_lock();
    uint32_t dst = to ? to->sin_addr : s->remote;
    uint16_t dst_port = to ? net_ntohs(to->sin_port) : s->remote_port;
    uint32_t next_hop;
    netif_t *netif = ipv4_route(dst, &next_hop);
    if (s->shut_write)
    {
        result = -EPIPE;
    }
    else if (!to && !s->connected)
    {
        result = -EDESTADDRREQ;
    }
    else if (is_broadcast(dst) && !s->broadcast)
    {
        result = -EACCES;
    }
    else if (!netif)
    {
        result = -ENETUNREACH;
    }
    else if (header + total + IPV4_HEADER_SIZE > netif->mtu)
    {
        result = -EMSGSIZE; // fragments are not sent
    }
    else if (s->port == 0 && !inet_bind_any_port(s))
    {
        result = -EADDRINUSE;
    }
    else if (icmp)
    {
        if (buffer[0] != ICMP_ECHO_REQUEST)
        {
            result = -EINVAL;
        }
        else
        {
            // the identifier is the socket's port; the checksum is ours
            buffer[4] = (uint8_t)(s->port >> 8);
            buffer[5] = (uint8_t)s->port;
            buffer[2] = 0;
            buffer[3] = 0;
            uint16_t sum = checksum(buffer, (uint32_t)total);
            memcpy(buffer + 2, &sum, 2);
            result = ipv4_send(s->local, dst, IP_PROTO_ICMP, buffer, (uint32_t)total)
                         ? (int32_t)total
                         : -ENOBUFS;
        }
    }
    else
    {
        result = udp_send(s->local, s->port, dst, dst_port, buffer, header + (uint32_t)total)
                     ? (int32_t)total
                     : -ENOBUFS;
    }
    net_unlock(lock);
    kfree(buffer);
    return result;
}

int32_t inet_socket_receive(file_t *file, const kiovec_t *iov, uint32_t iov_count,
                            ksockaddr_in_t *from, uint32_t flags, bool dontwait,
                            uint32_t *full)
{
    inet_socket_t *s = inet_socket_of(file);
    if (!s)
    {
        return -ENOTSOCK;
    }
    if (s->protocol == IPPROTO_TCP)
    {
        int32_t n = tcp_socket_receive(file, s, iov, iov_count, flags, dontwait);
        if (from)
        {
            memset(from, 0, sizeof(*from));
            from->sin_family = AF_INET;
            from->sin_port = net_htons(s->remote_port);
            from->sin_addr = s->remote;
        }
        if (full)
        {
            *full = n > 0 ? (uint32_t)n : 0;
        }
        return n;
    }
    bool peek = (flags & MSG_PEEK) != 0;
    uint32_t lock = net_lock();
    while (!s->head && !s->shut_read)
    {
        if (dontwait || (file->flags & O_NONBLOCK))
        {
            net_unlock(lock);
            return -EAGAIN;
        }
        if (signal_pending(process_current()))
        {
            net_unlock(lock);
            return -ERESTARTSYS;
        }
        net_wait(&s->waiters);
    }
    datagram_t *d = s->head;
    if (d && !peek)
    {
        s->head = d->next;
        if (!s->head)
        {
            s->tail = NULL;
        }
        s->queued -= d->length;
    }
    if (d && peek)
    {
        // a copy, so the datagram can be read while the lock is let go
        datagram_t *copy = kmalloc(sizeof(*d) + d->length);
        if (copy)
        {
            memcpy(copy, d, sizeof(*d) + d->length);
        }
        d = copy;
    }
    net_unlock(lock);
    if (!d)
    {
        return s->shut_read ? 0 : -ENOMEM;
    }

    uint32_t at = 0;
    int32_t result = 0;
    for (uint32_t i = 0; i < iov_count && at < d->length; i++)
    {
        uint32_t chunk = d->length - at < iov[i].len ? d->length - at : (uint32_t)iov[i].len;
        if (kerr_failed(copy_to_user((void *)iov[i].base, d->data + at, chunk)))
        {
            result = -EFAULT;
            break;
        }
        at += chunk;
    }
    if (from)
    {
        memset(from, 0, sizeof(*from));
        from->sin_family = AF_INET;
        from->sin_port = net_htons(d->from_port);
        from->sin_addr = d->from;
    }
    if (full)
    {
        *full = d->length;
    }
    kfree(d);
    return result != 0 ? result : (int32_t)at;
}

int32_t inet_socket_name(inet_socket_t *s, bool peer, ksockaddr_in_t *out)
{
    int32_t result = 0;
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    uint32_t lock = net_lock();
    if (s->conn && s->conn->state != TCP_LISTEN)
    {
        // a TCP connection: its own addresses
        out->sin_addr = peer ? s->conn->remote : s->conn->local;
        out->sin_port = net_htons(peer ? s->conn->remote_port : s->conn->local_port);
    }
    else if (peer)
    {
        if (!s->connected || s->protocol == IPPROTO_TCP)
        {
            result = -ENOTCONN;
        }
        out->sin_addr = s->remote;
        out->sin_port = net_htons(s->remote_port);
    }
    else
    {
        // a socket bound to any address and connected: the address it sends from
        out->sin_addr = s->local == 0 && s->connected ? ipv4_source_for(s->remote) : s->local;
        out->sin_port = net_htons(s->port);
    }
    net_unlock(lock);
    return result;
}

int32_t inet_socket_set_option(inet_socket_t *s, int32_t level, int32_t name, int32_t value)
{
    if (level == IPPROTO_TCP && s->protocol == IPPROTO_TCP)
    {
        // TCP_NODELAY: small writes are always sent at once; TCP_KEEPIDLE and
        // the others are accepted and not used
        return name >= TCP_NODELAY && name <= TCP_KEEPCNT ? 0 : -ENOPROTOOPT;
    }
    if (level != SOL_SOCKET)
    {
        return -ENOPROTOOPT;
    }
    switch (name)
    {
    case SO_REUSEADDR:
        s->reuse_address = value != 0;
        return 0;
    case SO_BROADCAST:
        s->broadcast = value != 0;
        return 0;
    case SO_SNDBUF:
    case SO_RCVBUF:
    case SO_KEEPALIVE:
        return 0; // accepted; the sizes are fixed
    default:
        return -ENOPROTOOPT;
    }
}

int32_t inet_socket_get_option(inet_socket_t *s, int32_t level, int32_t name, int32_t *value)
{
    if (level == IPPROTO_TCP && s->protocol == IPPROTO_TCP)
    {
        if (name == TCP_NODELAY)
        {
            *value = 1;
            return 0;
        }
        return -ENOPROTOOPT;
    }
    if (level != SOL_SOCKET)
    {
        return -ENOPROTOOPT;
    }
    switch (name)
    {
    case SO_REUSEADDR:
        *value = s->reuse_address;
        return 0;
    case SO_BROADCAST:
        *value = s->broadcast;
        return 0;
    case SO_TYPE:
        *value = s->protocol == IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM;
        return 0;
    case SO_ERROR:
    {
        uint32_t lock = net_lock();
        *value = 0;
        if (s->conn)
        {
            *value = -s->conn->error;
            s->conn->error = 0;
        }
        net_unlock(lock);
        return 0;
    }
    case SO_KEEPALIVE:
        *value = 0;
        return 0;
    case SO_SNDBUF:
    case SO_RCVBUF:
        *value = s->protocol == IPPROTO_TCP ? TCP_BUFFER_SIZE : INET_RECEIVE_MAX;
        return 0;
    default:
        return -ENOPROTOOPT;
    }
}

int32_t inet_socket_shutdown(inet_socket_t *s, int32_t how)
{
    if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR)
    {
        return -EINVAL;
    }
    uint32_t lock = net_lock();
    if (s->protocol == IPPROTO_TCP)
    {
        int32_t result = tcp_socket_shutdown(s, how);
        net_unlock(lock);
        return result;
    }
    if (how != SHUT_WR)
    {
        s->shut_read = true;
        wait_queue_wake_all(&s->waiters);
    }
    if (how != SHUT_RD)
    {
        s->shut_write = true;
    }
    net_unlock(lock);
    poll_notify();
    return 0;
}

/**
 * put a copy of a datagram on a socket's queue (net_lock held).
 */
static void enqueue(inet_socket_t *s, uint32_t from, uint16_t from_port, const uint8_t *data,
                    uint32_t length)
{
    if (s->shut_read || s->queued + length > INET_RECEIVE_MAX)
    {
        return; // full: dropped, as UDP may
    }
    datagram_t *d = kmalloc(sizeof(*d) + length);
    if (!d)
    {
        return;
    }
    d->next = NULL;
    d->from = from;
    d->from_port = from_port;
    d->length = length;
    memcpy(d->data, data, length);
    if (s->tail)
    {
        s->tail->next = d;
    }
    else
    {
        s->head = d;
    }
    s->tail = d;
    s->queued += length;
    wait_queue_wake_all(&s->waiters);
    poll_notify();
}

/**
 * true if a UDP socket takes a datagram from src:src_port to dst:dst_port.
 */
static bool takes(const inet_socket_t *s, uint32_t dst, uint16_t dst_port, uint32_t src,
                  uint16_t src_port, bool broadcast)
{
    if (s->protocol != IPPROTO_UDP || s->port != dst_port)
    {
        return false;
    }
    if (s->local != 0 && s->local != dst && !broadcast)
    {
        return false;
    }
    return !s->connected || (s->remote == src && s->remote_port == src_port);
}

bool inet_socket_deliver_udp(uint32_t dst, uint16_t dst_port, uint32_t src, uint16_t src_port,
                             const uint8_t *data, uint32_t length)
{
    bool broadcast = is_broadcast(dst);
    inet_socket_t *best = NULL;
    for (inet_socket_t *s = sockets; s; s = s->next)
    {
        if (!takes(s, dst, dst_port, src, src_port, broadcast))
        {
            continue;
        }
        if (broadcast)
        {
            enqueue(s, src, src_port, data, length); // every socket on the port
            best = s;
        }
        else if (!best || (s->local != 0 && best->local == 0) || (s->connected && !best->connected))
        {
            best = s; // the most exact match
        }
    }
    if (best && !broadcast)
    {
        enqueue(best, src, src_port, data, length);
    }
    return best != NULL;
}

void inet_socket_deliver_echo(uint32_t src, uint16_t id, const uint8_t *message,
                              uint32_t length)
{
    for (inet_socket_t *s = sockets; s; s = s->next)
    {
        if (s->protocol == IPPROTO_ICMP && s->port == id &&
            (!s->connected || s->remote == src))
        {
            enqueue(s, src, 0, message, length);
            return;
        }
    }
}

static int32_t inet_read(file_t *file, void *buf, uint32_t count)
{
    kiovec_t iov = {(uintptr_t)buf, count};
    return inet_socket_receive(file, &iov, 1, NULL, 0, false, NULL);
}

static int32_t inet_write(file_t *file, const void *buf, uint32_t count)
{
    kiovec_t iov = {(uintptr_t)buf, count};
    return inet_socket_send(file, &iov, 1, NULL, 0);
}

static int32_t inet_ioctl(file_t *file, uint32_t request, void *arg)
{
    inet_socket_t *s = file->device_data;
    if (request == FIONREAD)
    {
        // the length of the next datagram, or the bytes a stream has
        uint32_t lock = net_lock();
        int32_t length = s->head ? (int32_t)s->head->length : 0;
        if (s->conn && s->conn->state != TCP_LISTEN)
        {
            length = (int32_t)s->conn->receive_count;
        }
        net_unlock(lock);
        return kerr_to_errno(copy_to_user(arg, &length, sizeof(length)));
    }
    return netif_ioctl(request, arg);
}

static uint32_t inet_poll(file_t *file)
{
    inet_socket_t *s = file->device_data;
    uint32_t lock = net_lock();
    if (s->protocol == IPPROTO_TCP)
    {
        uint32_t events = tcp_socket_poll(s);
        net_unlock(lock);
        return events;
    }
    uint32_t events = s->shut_write ? 0 : POLLOUT;
    if (s->head || s->shut_read)
    {
        events |= POLLIN;
    }
    net_unlock(lock);
    return events;
}

static int32_t inet_stat(file_t *file, struct kstat *st)
{
    (void)file;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFSOCK | 0600;
    st->st_nlink = 1;
    st->st_blksize = ETH_MTU;
    return 0;
}

static void inet_release(file_t *file)
{
    inet_socket_t *s = file->device_data;
    uint32_t lock = net_lock();
    for (inet_socket_t **p = &sockets; *p; p = &(*p)->next)
    {
        if (*p == s)
        {
            *p = s->next;
            break;
        }
    }
    if (s->conn)
    {
        tcp_release(s->conn); // the connection closes, and is freed later
    }
    net_unlock(lock);
    while (s->head)
    {
        datagram_t *d = s->head;
        s->head = d->next;
        kfree(d);
    }
    kfree(s);
    kfree(file);
}

static const file_ops_t inet_file_ops = {
    .read = inet_read,
    .write = inet_write,
    .stat = inet_stat,
    .poll = inet_poll,
    .ioctl = inet_ioctl,
    .release = inet_release,
};

int32_t inet_socket_listen(inet_socket_t *s, uint32_t backlog)
{
    return s->protocol == IPPROTO_TCP ? tcp_socket_listen(s, backlog) : -EOPNOTSUPP;
}

int32_t inet_socket_accept(file_t *file, uint32_t flags, file_t **out, ksockaddr_in_t *peer)
{
    return tcp_socket_accept(file, flags, out, peer);
}
