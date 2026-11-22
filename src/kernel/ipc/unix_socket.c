/**
 * Unix domain stream sockets (see unix_socket.h).
 *
 * each socket has a receive buffer; its peer writes into it. the files
 * passed with a message wait in a list, each at the stream position of
 * the message's first byte, and a receive hands them out when it reaches
 * that position.
 *
 * lifetime: a connected socket points to its peer and the peer back.
 * when a socket's last file reference goes (release), it unhooks itself
 * from its peer under the lock and is freed; the peer then reads the end
 * of the stream and its writes fail with EPIPE. a connection a listener
 * has not accepted yet has no file; it goes with the listener.
 *
 * waiting: everyone who waits for something on a socket (data to read,
 * room in the peer's buffer, a connection to accept) sleeps on that
 * socket's own queue, so no one sleeps on a socket that can be freed.
 *
 * locking: one spinlock for all sockets. user memory is copied with it
 * held, as for pipes (the syscall layer has checked the ranges).
 */

#include "unix_socket.h"

#include "common/memory.h"
#include "common/string.h"
#include "kernel/errno.h"
#include "kernel/memory/heap.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"
#include "kernel/sync/spinlock.h"
#include "kernel/sync/wait_queue.h"
#include "kernel/vfs/file.h"
#include "kernel/vfs/poll.h"
#include "kernel/vfs/stat.h"

#define BUFFER_SIZE   16384
#define MAX_BACKLOG   16
#define MAX_NAMES     32

typedef struct rights
{
    uint64_t at; // the stream position of the message's first byte
    uint32_t count;
    file_t *files[SOCKET_MAX_FDS];
    struct rights *next;
} rights_t;

typedef enum
{
    STATE_NEW,
    STATE_LISTENING,
    STATE_CONNECTED,
} state_t;

struct unix_socket
{
    state_t state;
    bool bound;
    char path[UNIX_PATH_MAX];
    struct unix_socket *peer; // NULL: not connected, or the peer is gone
    bool peer_gone;           // the peer closed: read the rest, then EOF

    uint8_t *buffer;          // the receive buffer
    uint32_t head;
    uint32_t count;
    uint64_t read_pos;        // stream positions (bytes received and read)
    uint64_t write_pos;
    rights_t *rights;         // passed files in stream order
    rights_t *rights_tail;

    struct unix_socket *pending[MAX_BACKLOG]; // connections to accept
    uint32_t pending_count;
    uint32_t backlog;

    wait_queue_t waiters;
};

static spinlock_t lock;
static bool lock_ready;
static unix_socket_t *names[MAX_NAMES]; // the bound sockets

static const file_ops_t socket_file_ops;

static void lock_init_once(void)
{
    if (!lock_ready)
    {
        spinlock_init(&lock);
        lock_ready = true;
    }
}

static unix_socket_t *socket_new(void)
{
    unix_socket_t *s = kmalloc(sizeof(*s));
    if (!s)
    {
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    s->buffer = kmalloc(BUFFER_SIZE);
    if (!s->buffer)
    {
        kfree(s);
        return NULL;
    }
    wait_queue_init(&s->waiters);
    return s;
}

static file_t *file_for(unix_socket_t *s, uint32_t flags)
{
    file_t *file = file_new(&socket_file_ops, O_RDWR | (flags & O_NONBLOCK));
    if (file)
    {
        file->device_data = s;
    }
    return file;
}

/**
 * take the passed files that are still queued (the caller drops their
 * references outside the lock). call with the lock held.
 */
static rights_t *take_rights(unix_socket_t *s)
{
    rights_t *list = s->rights;
    s->rights = NULL;
    s->rights_tail = NULL;
    return list;
}

static void drop_rights(rights_t *list)
{
    while (list)
    {
        rights_t *next = list->next;
        for (uint32_t i = 0; i < list->count; i++)
        {
            file_unref(list->files[i]);
        }
        kfree(list);
        list = next;
    }
}

/**
 * unhook a socket from its peer and its name. call with the lock held.
 * @return the files still queued in it (to drop after the lock).
 */
static rights_t *detach(unix_socket_t *s)
{
    if (s->peer)
    {
        s->peer->peer = NULL;
        s->peer->peer_gone = true;
        wait_queue_wake_all(&s->peer->waiters);
        s->peer = NULL;
    }
    if (s->bound)
    {
        for (uint32_t i = 0; i < MAX_NAMES; i++)
        {
            if (names[i] == s)
            {
                names[i] = NULL;
            }
        }
        s->bound = false;
    }
    return take_rights(s);
}

static void socket_free(unix_socket_t *s)
{
    kfree(s->buffer);
    kfree(s);
}

unix_socket_t *unix_socket_of(file_t *file)
{
    return file && file->ops == &socket_file_ops ? file->device_data : NULL;
}

static void socket_release(file_t *file)
{
    unix_socket_t *s = file->device_data;
    unix_socket_t *pending[MAX_BACKLOG];
    rights_t *dropped[MAX_BACKLOG + 1];
    uint32_t pending_count;

    uint32_t flags = spin_lock_irqsave(&lock);
    dropped[0] = detach(s);
    pending_count = s->pending_count;
    for (uint32_t i = 0; i < pending_count; i++)
    {
        pending[i] = s->pending[i];
        dropped[i + 1] = detach(pending[i]); // its client sees the end
    }
    s->pending_count = 0;
    spin_unlock_irqrestore(&lock, flags);

    for (uint32_t i = 0; i <= pending_count; i++)
    {
        drop_rights(dropped[i]);
    }
    for (uint32_t i = 0; i < pending_count; i++)
    {
        socket_free(pending[i]);
    }
    socket_free(s);
    kfree(file);
}

static int32_t socket_read(file_t *file, void *buf, uint32_t count)
{
    kiovec_t iov = {(uintptr_t)buf, count};
    file_t *files[SOCKET_MAX_FDS];
    uint32_t file_count = 0;
    int32_t n = unix_socket_receive(file, &iov, 1, files, &file_count, false);
    for (uint32_t i = 0; i < file_count; i++)
    {
        file_unref(files[i]); // read() has no room for passed files
    }
    return n;
}

static int32_t socket_write(file_t *file, const void *buf, uint32_t count)
{
    kiovec_t iov = {(uintptr_t)buf, count};
    return unix_socket_send(file, &iov, 1, NULL, 0, false);
}

static uint32_t socket_poll(file_t *file)
{
    unix_socket_t *s = file->device_data;
    uint32_t events = 0;
    uint32_t flags = spin_lock_irqsave(&lock);
    if (s->state == STATE_LISTENING)
    {
        events |= s->pending_count > 0 ? POLLIN : 0;
    }
    else if (s->state == STATE_CONNECTED)
    {
        if (s->count > 0 || s->peer_gone)
        {
            events |= POLLIN;
        }
        if (s->peer_gone)
        {
            events |= POLLHUP;
        }
        else if (s->peer && s->peer->count < BUFFER_SIZE)
        {
            events |= POLLOUT;
        }
    }
    spin_unlock_irqrestore(&lock, flags);
    return events;
}

static int32_t socket_stat(file_t *file, struct kstat *st)
{
    (void)file;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFSOCK | 0600;
    st->st_nlink = 1;
    st->st_blksize = BUFFER_SIZE;
    return 0;
}

static const file_ops_t socket_file_ops = {
    .read = socket_read,
    .write = socket_write,
    .stat = socket_stat,
    .poll = socket_poll,
    .release = socket_release,
};

kerr_t unix_socket_create(uint32_t flags, file_t **out)
{
    lock_init_once();
    unix_socket_t *s = socket_new();
    if (!s)
    {
        return KERR_NOMEM;
    }
    file_t *file = file_for(s, flags);
    if (!file)
    {
        socket_free(s);
        return KERR_NOMEM;
    }
    *out = file;
    return KERR_OK;
}

kerr_t unix_socket_pair(uint32_t flags, file_t **a, file_t **b)
{
    if (kerr_failed(unix_socket_create(flags, a)))
    {
        return KERR_NOMEM;
    }
    if (kerr_failed(unix_socket_create(flags, b)))
    {
        file_unref(*a);
        return KERR_NOMEM;
    }
    unix_socket_t *sa = (*a)->device_data;
    unix_socket_t *sb = (*b)->device_data;
    sa->peer = sb;
    sb->peer = sa;
    sa->state = STATE_CONNECTED;
    sb->state = STATE_CONNECTED;
    return KERR_OK;
}

int32_t unix_socket_bind(unix_socket_t *s, const char *path)
{
    if (path[0] == '\0')
    {
        return -EINVAL;
    }
    int32_t result = -ENOBUFS;
    uint32_t flags = spin_lock_irqsave(&lock);
    if (s->bound || s->state != STATE_NEW)
    {
        result = -EINVAL;
    }
    else
    {
        int32_t free_slot = -1;
        result = 0;
        for (uint32_t i = 0; i < MAX_NAMES; i++)
        {
            if (names[i] && strcmp(names[i]->path, path) == 0)
            {
                result = -EADDRINUSE;
                break;
            }
            if (!names[i] && free_slot < 0)
            {
                free_slot = (int32_t)i;
            }
        }
        if (result == 0 && free_slot < 0)
        {
            result = -ENOBUFS;
        }
        if (result == 0)
        {
            strncpy(s->path, path, UNIX_PATH_MAX - 1);
            s->path[UNIX_PATH_MAX - 1] = '\0';
            s->bound = true;
            names[free_slot] = s;
        }
    }
    spin_unlock_irqrestore(&lock, flags);
    return result;
}

int32_t unix_socket_listen(unix_socket_t *s, uint32_t backlog)
{
    int32_t result = 0;
    uint32_t flags = spin_lock_irqsave(&lock);
    if (!s->bound || (s->state != STATE_NEW && s->state != STATE_LISTENING))
    {
        result = -EINVAL;
    }
    else
    {
        s->state = STATE_LISTENING;
        s->backlog = backlog == 0 || backlog > MAX_BACKLOG ? MAX_BACKLOG : backlog;
    }
    spin_unlock_irqrestore(&lock, flags);
    return result;
}

int32_t unix_socket_connect(unix_socket_t *s, const char *path)
{
    // the server's end of the connection, made before the lock is taken
    unix_socket_t *server_end = socket_new();
    if (!server_end)
    {
        return -ENOMEM;
    }

    int32_t result = -ECONNREFUSED;
    uint32_t flags = spin_lock_irqsave(&lock);
    if (s->state != STATE_NEW)
    {
        result = s->state == STATE_CONNECTED ? -EISCONN : -EINVAL;
    }
    else
    {
        for (uint32_t i = 0; i < MAX_NAMES; i++)
        {
            unix_socket_t *l = names[i];
            if (!l || strcmp(l->path, path) != 0)
            {
                continue;
            }
            if (l->state == STATE_LISTENING && l->pending_count < l->backlog)
            {
                server_end->state = STATE_CONNECTED;
                server_end->peer = s;
                s->peer = server_end;
                s->state = STATE_CONNECTED;
                l->pending[l->pending_count++] = server_end;
                wait_queue_wake_all(&l->waiters);
                server_end = NULL;
                result = 0;
            }
            break;
        }
    }
    spin_unlock_irqrestore(&lock, flags);

    if (server_end)
    {
        socket_free(server_end); // not used
    }
    if (result == 0)
    {
        poll_notify(); // the listener became readable
    }
    return result;
}

int32_t unix_socket_accept(file_t *listener, uint32_t new_flags, file_t **out)
{
    unix_socket_t *l = unix_socket_of(listener);
    if (!l)
    {
        return -ENOTSOCK;
    }

    // the file for the new connection, made before the lock is taken
    file_t *file = file_new(&socket_file_ops, O_RDWR | (new_flags & O_NONBLOCK));
    if (!file)
    {
        return -ENOMEM;
    }

    uint32_t flags = spin_lock_irqsave(&lock);
    for (;;)
    {
        if (l->state != STATE_LISTENING)
        {
            spin_unlock_irqrestore(&lock, flags);
            kfree(file);
            return -EINVAL;
        }
        if (l->pending_count > 0)
        {
            break;
        }
        if (listener->flags & O_NONBLOCK)
        {
            spin_unlock_irqrestore(&lock, flags);
            kfree(file);
            return -EAGAIN;
        }
        if (signal_pending(process_current()))
        {
            spin_unlock_irqrestore(&lock, flags);
            kfree(file);
            return -ERESTARTSYS;
        }
        wait_queue_block_locked_interruptible(&l->waiters, &lock);
    }

    unix_socket_t *s = l->pending[0];
    l->pending_count--;
    memmove(&l->pending[0], &l->pending[1], l->pending_count * sizeof(l->pending[0]));
    file->device_data = s;
    spin_unlock_irqrestore(&lock, flags);

    *out = file;
    return 0;
}

/**
 * wait on a socket's queue with the lock held.
 * @return 0 to try again, or -EAGAIN / -ERESTARTSYS.
 */
static int32_t wait_on(unix_socket_t *s, file_t *file, bool dontwait)
{
    if (dontwait || (file->flags & O_NONBLOCK))
    {
        return -EAGAIN;
    }
    if (signal_pending(process_current()))
    {
        return -ERESTARTSYS;
    }
    wait_queue_block_locked_interruptible(&s->waiters, &lock);
    return 0;
}

int32_t unix_socket_send(file_t *file, const kiovec_t *iov, uint32_t iov_count, file_t **files,
                         uint32_t file_count, bool dontwait)
{
    unix_socket_t *s = unix_socket_of(file);
    if (!s)
    {
        return -ENOTSOCK;
    }

    // the record of the passed files, made before the lock is taken
    rights_t *rights = NULL;
    if (file_count > 0)
    {
        rights = kmalloc(sizeof(*rights));
        if (!rights)
        {
            return -ENOMEM;
        }
        rights->count = file_count;
        rights->next = NULL;
        for (uint32_t i = 0; i < file_count; i++)
        {
            file_ref(files[i]);
            rights->files[i] = files[i];
        }
    }

    uint32_t sent = 0;
    int32_t result = 0;
    uint32_t flags = spin_lock_irqsave(&lock);
    for (uint32_t v = 0; v < iov_count && result == 0; v++)
    {
        const uint8_t *src = (const uint8_t *)iov[v].base;
        size_t left = iov[v].len;
        while (left > 0)
        {
            if (s->state != STATE_CONNECTED)
            {
                result = -ENOTCONN;
                break;
            }
            unix_socket_t *peer = s->peer;
            if (!peer)
            {
                result = -EPIPE;
                break;
            }
            uint32_t room = BUFFER_SIZE - peer->count;
            if (room == 0)
            {
                // the reader wakes this socket's queue when it frees room.
                // a non-blocking send that got some bytes in returns them
                result = wait_on(s, file, dontwait);
                if (result != 0)
                {
                    break;
                }
                continue;
            }

            // the passed files go with the message's first byte
            if (rights && sent == 0)
            {
                rights->at = peer->write_pos;
                if (peer->rights_tail)
                {
                    peer->rights_tail->next = rights;
                }
                else
                {
                    peer->rights = rights;
                }
                peer->rights_tail = rights;
                rights = NULL;
            }

            uint32_t chunk = left < room ? (uint32_t)left : room;
            uint32_t tail = (peer->head + peer->count) % BUFFER_SIZE;
            uint32_t first = BUFFER_SIZE - tail < chunk ? BUFFER_SIZE - tail : chunk;
            memcpy(peer->buffer + tail, src, first);
            memcpy(peer->buffer, src + first, chunk - first);
            peer->count += chunk;
            peer->write_pos += chunk;
            src += chunk;
            left -= chunk;
            sent += chunk;
            wait_queue_wake_all(&peer->waiters);
        }
    }
    spin_unlock_irqrestore(&lock, flags);

    if (rights)
    {
        drop_rights(rights); // nothing was sent
    }
    if (sent > 0)
    {
        poll_notify();
        return (int32_t)sent;
    }
    return result;
}

int32_t unix_socket_receive(file_t *file, const kiovec_t *iov, uint32_t iov_count, file_t **files,
                            uint32_t *file_count, bool dontwait)
{
    unix_socket_t *s = unix_socket_of(file);
    *file_count = 0;
    if (!s)
    {
        return -ENOTSOCK;
    }

    uint32_t flags = spin_lock_irqsave(&lock);
    if (s->state != STATE_CONNECTED)
    {
        spin_unlock_irqrestore(&lock, flags);
        return -ENOTCONN;
    }
    while (s->count == 0)
    {
        if (s->peer_gone)
        {
            spin_unlock_irqrestore(&lock, flags);
            return 0; // the end of the stream
        }
        int32_t err = wait_on(s, file, dontwait);
        if (err != 0)
        {
            spin_unlock_irqrestore(&lock, flags);
            return err;
        }
    }

    // the files of a message that starts here come with this call; the
    // call stops before the next message that passes files
    rights_t *taken = NULL;
    if (s->rights && s->rights->at == s->read_pos)
    {
        taken = s->rights;
        s->rights = taken->next;
        if (!s->rights)
        {
            s->rights_tail = NULL;
        }
    }
    uint64_t limit = s->count;
    if (s->rights && s->rights->at - s->read_pos < limit)
    {
        limit = s->rights->at - s->read_pos;
    }

    uint32_t received = 0;
    for (uint32_t v = 0; v < iov_count && received < limit; v++)
    {
        uint8_t *dst = (uint8_t *)iov[v].base;
        uint64_t want = iov[v].len < limit - received ? iov[v].len : limit - received;
        uint32_t chunk = (uint32_t)want;
        uint32_t first = BUFFER_SIZE - s->head < chunk ? BUFFER_SIZE - s->head : chunk;
        memcpy(dst, s->buffer + s->head, first);
        memcpy(dst + first, s->buffer, chunk - first);
        s->head = (s->head + chunk) % BUFFER_SIZE;
        s->count -= chunk;
        s->read_pos += chunk;
        received += chunk;
    }

    // room in this buffer: the peer's writers wait on the peer's queue
    if (received > 0 && s->peer)
    {
        wait_queue_wake_all(&s->peer->waiters);
    }
    spin_unlock_irqrestore(&lock, flags);

    if (taken)
    {
        for (uint32_t i = 0; i < taken->count; i++)
        {
            files[i] = taken->files[i];
        }
        *file_count = taken->count;
        kfree(taken);
    }
    if (received > 0)
    {
        poll_notify();
    }
    return (int32_t)received;
}
