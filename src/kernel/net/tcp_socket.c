/**
 * @file tcp_socket.c
 * @brief TCP sockets (SOCK_STREAM): the socket calls over tcp.c.
 *
 * user data goes through a kernel buffer of CHUNK bytes: it is copied from
 * user memory before net_lock is taken, and to user memory after the lock
 * is let go, as a copy can fault and sleep.
 */

#include "byteorder.h"
#include "common/memory.h"
#include "inet_internal.h"
#include "kernel/errno.h"
#include "kernel/memory/heap.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"
#include "kernel/usermode/uaccess.h"
#include "kernel/vfs/file.h"
#include "kernel/vfs/poll.h"
#include "netif.h"

#define CHUNK (32 * 1024)

/**
 * copy length bytes from the iovecs, starting offset bytes in, to dst.
 */
static int32_t iov_copy_in(uint8_t *dst, const kiovec_t *iov, uint32_t iov_count, size_t offset,
                           uint32_t length)
{
    for (uint32_t i = 0; i < iov_count && length > 0; i++)
    {
        if (offset >= iov[i].len)
        {
            offset -= iov[i].len;
            continue;
        }
        size_t chunk = iov[i].len - offset < length ? iov[i].len - offset : length;
        if (kerr_failed(copy_from_user(dst, (const uint8_t *)iov[i].base + offset, chunk)))
        {
            return -EFAULT;
        }
        dst += chunk;
        length -= (uint32_t)chunk;
        offset = 0;
    }
    return 0;
}

/**
 * copy length bytes from src into the iovecs.
 */
static int32_t iov_copy_out(const kiovec_t *iov, uint32_t iov_count, const uint8_t *src,
                            uint32_t length)
{
    for (uint32_t i = 0; i < iov_count && length > 0; i++)
    {
        size_t chunk = iov[i].len < length ? iov[i].len : length;
        if (chunk > 0 && kerr_failed(copy_to_user((void *)iov[i].base, src, chunk)))
        {
            return -EFAULT;
        }
        src += chunk;
        length -= (uint32_t)chunk;
    }
    return 0;
}

/**
 * true while the handshake is not done.
 */
static bool connecting(const tcp_conn_t *c)
{
    return c->state == TCP_SYN_SENT || c->state == TCP_SYN_RECEIVED;
}

/**
 * the error of a connection that failed, once; then 0.
 */
static int32_t take_error(tcp_conn_t *c)
{
    int32_t error = c->error;
    c->error = 0;
    return error;
}

int32_t tcp_socket_connect(file_t *file, inet_socket_t *s, const ksockaddr_in_t *addr)
{
    if (addr->sin_family != AF_INET)
    {
        return -EAFNOSUPPORT;
    }
    uint32_t lock = net_lock();
    tcp_conn_t *c = s->conn;
    int32_t result = 0;
    if (c && c->state == TCP_CLOSED)
    {
        tcp_release(c); // a connection that failed: start again
        s->conn = c = NULL;
    }
    if (c)
    {
        result = c->state == TCP_LISTEN ? -EINVAL : connecting(c) ? -EALREADY : -EISCONN;
    }
    else if (addr->sin_addr == 0 || addr->sin_addr == IPV4_BROADCAST)
    {
        result = -ENETUNREACH;
    }
    else if (ipv4_source_for(addr->sin_addr) == 0)
    {
        result = -ENETUNREACH;
    }
    else if (s->port == 0 && !inet_bind_any_port(s))
    {
        result = -EADDRNOTAVAIL;
    }
    else
    {
        uint32_t local = s->local != 0 ? s->local : ipv4_source_for(addr->sin_addr);
        c = tcp_connect(local, s->port, addr->sin_addr, net_ntohs(addr->sin_port));
        if (!c)
        {
            result = -ENOMEM;
        }
        else
        {
            s->conn = c;
            s->remote = addr->sin_addr;
            s->remote_port = net_ntohs(addr->sin_port);
        }
    }
    if (result != 0)
    {
        net_unlock(lock);
        return result;
    }

    // wait for the handshake
    while (connecting(c))
    {
        if (file->flags & O_NONBLOCK)
        {
            net_unlock(lock);
            return -EINPROGRESS;
        }
        if (signal_pending(process_current()))
        {
            net_unlock(lock);
            return -EINTR; // the connection goes on
        }
        net_wait(&c->waiters);
    }
    if (c->state == TCP_CLOSED)
    {
        result = take_error(c);
        if (result == 0)
        {
            result = -ECONNREFUSED;
        }
        tcp_release(c);
        s->conn = NULL;
    }
    else
    {
        s->connected = true;
    }
    net_unlock(lock);
    return result;
}

int32_t tcp_socket_listen(inet_socket_t *s, uint32_t backlog)
{
    int32_t result = 0;
    uint32_t lock = net_lock();
    if (s->conn && s->conn->state == TCP_LISTEN)
    {
        s->conn->backlog = backlog == 0 ? 1 : backlog > TCP_BACKLOG_MAX ? TCP_BACKLOG_MAX : backlog;
    }
    else if (s->conn)
    {
        result = -EINVAL;
    }
    else if (s->port == 0 && !inet_bind_any_port(s))
    {
        result = -EADDRINUSE;
    }
    else
    {
        s->conn = tcp_listen(s->local, s->port, backlog);
        result = s->conn ? 0 : -ENOMEM;
    }
    net_unlock(lock);
    return result;
}

int32_t tcp_socket_accept(file_t *file, uint32_t flags, file_t **out, ksockaddr_in_t *peer)
{
    inet_socket_t *s = inet_socket_of(file);
    if (!s || s->protocol != IPPROTO_TCP)
    {
        return -EOPNOTSUPP;
    }
    // the new socket first: making it takes net_lock
    file_t *new_file;
    inet_socket_t *n;
    int32_t result = inet_socket_alloc(IPPROTO_TCP, flags, &new_file, &n);
    if (result != 0)
    {
        return result;
    }
    uint32_t lock = net_lock();
    tcp_conn_t *l = s->conn;
    tcp_conn_t *c = NULL;
    while (result == 0)
    {
        if (!l || l->state != TCP_LISTEN)
        {
            result = -EINVAL;
        }
        else if ((c = tcp_accept(l)) != NULL)
        {
            break;
        }
        else if (file->flags & O_NONBLOCK)
        {
            result = -EAGAIN;
        }
        else if (signal_pending(process_current()))
        {
            result = -ERESTARTSYS;
        }
        else
        {
            net_wait(&l->waiters);
        }
    }
    if (c)
    {
        n->conn = c;
        n->local = c->local;
        n->port = c->local_port;
        n->remote = c->remote;
        n->remote_port = c->remote_port;
        n->connected = true;
    }
    net_unlock(lock);
    if (result != 0)
    {
        file_unref(new_file);
        return result;
    }
    memset(peer, 0, sizeof(*peer));
    peer->sin_family = AF_INET;
    peer->sin_addr = c->remote;
    peer->sin_port = net_htons(c->remote_port);
    *out = new_file;
    return 0;
}

int32_t tcp_socket_send(file_t *file, inet_socket_t *s, const kiovec_t *iov, uint32_t iov_count,
                        bool dontwait)
{
    size_t total = 0;
    for (uint32_t i = 0; i < iov_count; i++)
    {
        total += iov[i].len;
    }
    if (!s->conn)
    {
        return -ENOTCONN;
    }
    if (total == 0)
    {
        return 0;
    }
    uint8_t *buffer = kmalloc(CHUNK);
    if (!buffer)
    {
        return -ENOMEM;
    }
    size_t sent = 0;
    int32_t result = 0;
    while (sent < total && result == 0)
    {
        uint32_t length = total - sent < CHUNK ? (uint32_t)(total - sent) : CHUNK;
        result = iov_copy_in(buffer, iov, iov_count, sent, length);
        if (result != 0)
        {
            break;
        }
        uint32_t done = 0;
        uint32_t lock = net_lock();
        tcp_conn_t *c = s->conn;
        while (done < length)
        {
            if (c->state == TCP_LISTEN)
            {
                result = -ENOTCONN;
                break;
            }
            if (!connecting(c) && !tcp_can_send(c))
            {
                result = c->error != 0 ? take_error(c) : -EPIPE;
                break;
            }
            uint32_t n = connecting(c) ? 0 : tcp_write(c, buffer + done, length - done);
            done += n;
            if (n > 0)
            {
                continue;
            }
            if (dontwait || (file->flags & O_NONBLOCK))
            {
                result = -EAGAIN;
                break;
            }
            if (signal_pending(process_current()))
            {
                result = -ERESTARTSYS;
                break;
            }
            net_wait(&c->waiters);
        }
        net_unlock(lock);
        sent += done;
    }
    kfree(buffer);
    // a part sent counts; the error comes with the next call
    if (sent > 0)
    {
        return (int32_t)sent;
    }
    return result == -ERESTARTSYS ? -EINTR : result;
}

int32_t tcp_socket_receive(file_t *file, inet_socket_t *s, const kiovec_t *iov,
                           uint32_t iov_count, uint32_t flags, bool dontwait)
{
    size_t total = 0;
    for (uint32_t i = 0; i < iov_count; i++)
    {
        total += iov[i].len;
    }
    if (!s->conn || s->conn->state == TCP_LISTEN)
    {
        return -ENOTCONN;
    }
    if (total == 0)
    {
        return 0;
    }
    uint32_t length = total < CHUNK ? (uint32_t)total : CHUNK;
    uint8_t *buffer = kmalloc(length);
    if (!buffer)
    {
        return -ENOMEM;
    }
    int32_t result = 0;
    uint32_t lock = net_lock();
    tcp_conn_t *c = s->conn;
    while (c->receive_count == 0)
    {
        if (c->fin_received || c->shut_read || c->state == TCP_CLOSED)
        {
            result = take_error(c); // 0: the end of the stream
            break;
        }
        if (dontwait || (file->flags & O_NONBLOCK))
        {
            result = -EAGAIN;
            break;
        }
        if (signal_pending(process_current()))
        {
            result = -ERESTARTSYS;
            break;
        }
        net_wait(&c->waiters);
    }
    uint32_t n = 0;
    if (result == 0)
    {
        n = tcp_read(c, buffer, length, (flags & MSG_PEEK) != 0);
    }
    net_unlock(lock);
    if (n > 0)
    {
        result = iov_copy_out(iov, iov_count, buffer, n);
    }
    kfree(buffer);
    return result != 0 ? result : (int32_t)n;
}

uint32_t tcp_socket_poll(inet_socket_t *s)
{
    tcp_conn_t *c = s->conn;
    if (!c)
    {
        return POLLOUT | POLLHUP;
    }
    if (c->state == TCP_LISTEN)
    {
        return c->queue_head ? POLLIN : 0;
    }
    if (connecting(c))
    {
        return 0;
    }
    uint32_t events = 0;
    if (c->receive_count > 0 || c->fin_received || c->shut_read || c->state == TCP_CLOSED)
    {
        events |= POLLIN;
    }
    if (tcp_can_send(c) && tcp_send_room(c) > 0)
    {
        events |= POLLOUT;
    }
    if (c->state == TCP_CLOSED)
    {
        events |= POLLHUP;
        if (c->error != 0)
        {
            events |= POLLERR | POLLOUT; // a connect that failed is done
        }
    }
    return events;
}

int32_t tcp_socket_shutdown(inet_socket_t *s, int32_t how)
{
    tcp_conn_t *c = s->conn;
    if (!c || c->state == TCP_LISTEN)
    {
        return -ENOTCONN;
    }
    if (how != SHUT_WR)
    {
        // what is in the buffer and what comes later is thrown away
        c->shut_read = true;
        c->receive_head = 0;
        c->receive_count = 0;
        wait_queue_wake_all(&c->waiters);
    }
    if (how != SHUT_RD)
    {
        tcp_shutdown_write(c);
    }
    poll_notify();
    return 0;
}
