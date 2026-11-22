/**
 * @file heimdall.c
 * @brief libheimdall: the client side of the heimdalld protocol.
 *
 * the socket is blocking for sends (64-byte messages, which the server
 * reads quickly) and polled for receives. files that come with messages
 * are kept in a queue until the message that uses them is read.
 */

#include "heimdall.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MSG_SIZE ((int)sizeof(heim_msg_t))
#define IN_BUFFER (MSG_SIZE * 32)
#define FD_QUEUE 8

struct heim_window
{
    heim_conn_t *conn;
    heim_window_t *next;
    uint32_t id;
    gfx_buffer_t buffer;
    void *map;
    size_t map_size;
    bool busy;
    void *data;
};

struct heim_conn
{
    int fd;
    int32_t screen_width;
    int32_t screen_height;
    uint32_t next_id;
    heim_window_t *windows;

    uint8_t in[IN_BUFFER];
    int in_used;
    int fds[FD_QUEUE];
    int fd_count;

    char *clipboard; // the text of the last HEIM_EV_CLIPBOARD
};

/**
 * send one message, with an fd when fd >= 0.
 */
static bool send_msg(heim_conn_t *conn, const heim_msg_t *msg, int fd)
{
    struct iovec iov = {(void *)msg, sizeof(*msg)};
    union
    {
        struct cmsghdr header;
        uint8_t space[CMSG_SPACE(sizeof(int))];
    } control;
    struct msghdr mh;
    memset(&mh, 0, sizeof(mh));
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    if (fd >= 0)
    {
        memset(&control, 0, sizeof(control));
        mh.msg_control = &control;
        mh.msg_controllen = CMSG_SPACE(sizeof(int));
        struct cmsghdr *c = CMSG_FIRSTHDR(&mh);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &fd, sizeof(int));
    }

    size_t done = 0;
    while (done < sizeof(*msg))
    {
        ssize_t n;
        if (done == 0)
            n = sendmsg(conn->fd, &mh, MSG_NOSIGNAL);
        else
            n = send(conn->fd, (const uint8_t *)msg + done, sizeof(*msg) - done, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        done += (size_t)n;
    }
    return true;
}

static void copy_title(char *out, const char *title)
{
    memset(out, 0, HEIM_TITLE_MAX);
    if (!title)
        return;
    size_t len = strlen(title);
    if (len > HEIM_TITLE_MAX - 1)
    {
        // cut at a character boundary
        len = HEIM_TITLE_MAX - 1;
        while (len > 0 && ((uint8_t)title[len] & 0xC0) == 0x80)
            len--;
    }
    memcpy(out, title, len);
}

/**
 * read what the socket has (or wait for it).
 * @return 1 when something came, 0 at the timeout, -1 when the server is gone.
 */
static int fill(heim_conn_t *conn, int timeout_ms)
{
    struct pollfd p = {conn->fd, POLLIN, 0};
    int ready;
    do
        ready = poll(&p, 1, timeout_ms);
    while (ready < 0 && errno == EINTR);
    if (ready < 0)
        return -1;
    if (ready == 0)
        return 0;

    struct iovec iov = {conn->in + conn->in_used, (size_t)(IN_BUFFER - conn->in_used)};
    union
    {
        struct cmsghdr header;
        uint8_t space[CMSG_SPACE(sizeof(int) * FD_QUEUE)];
    } control;
    struct msghdr mh;
    memset(&mh, 0, sizeof(mh));
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = &control;
    mh.msg_controllen = sizeof(control);
    ssize_t n = recvmsg(conn->fd, &mh, MSG_CMSG_CLOEXEC);
    if (n <= 0)
        return -1;
    conn->in_used += (int)n;

    for (struct cmsghdr *c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c))
    {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS)
            continue;
        int count = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
        for (int i = 0; i < count; i++)
        {
            int fd;
            memcpy(&fd, CMSG_DATA(c) + i * sizeof(int), sizeof(int));
            if (conn->fd_count < FD_QUEUE)
                conn->fds[conn->fd_count++] = fd;
            else
                close(fd);
        }
    }
    return 1;
}

static int take_fd(heim_conn_t *conn)
{
    if (conn->fd_count == 0)
        return -1;
    int fd = conn->fds[0];
    memmove(conn->fds, conn->fds + 1, (size_t)(conn->fd_count - 1) * sizeof(int));
    conn->fd_count--;
    return fd;
}

/**
 * take one whole message from the buffer, if there is one.
 */
static bool take_msg(heim_conn_t *conn, heim_msg_t *msg)
{
    if (conn->in_used < MSG_SIZE)
        return false;
    memcpy(msg, conn->in, MSG_SIZE);
    memmove(conn->in, conn->in + MSG_SIZE, (size_t)(conn->in_used - MSG_SIZE));
    conn->in_used -= MSG_SIZE;
    return true;
}

static heim_window_t *find_window(heim_conn_t *conn, uint32_t id)
{
    for (heim_window_t *w = conn->windows; w; w = w->next)
        if (w->id == id)
            return w;
    return NULL;
}

/**
 * read the clipboard text that came with a HEIM_EV_CLIPBOARD.
 */
static void read_clipboard(heim_conn_t *conn, heim_event_t *event)
{
    free(conn->clipboard);
    conn->clipboard = NULL;
    uint32_t length = event->msg.clip.length;
    int fd = length ? take_fd(conn) : -1;
    if (fd < 0 || length > HEIM_CLIPBOARD_MAX)
    {
        if (fd >= 0)
            close(fd);
        return;
    }
    void *map = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED)
        return;
    conn->clipboard = malloc(length + 1);
    if (conn->clipboard)
    {
        memcpy(conn->clipboard, map, length);
        conn->clipboard[length] = '\0';
        event->clipboard = conn->clipboard;
        event->clipboard_length = length;
    }
    munmap(map, length);
}

int heim_next_event(heim_conn_t *conn, heim_event_t *event, int timeout_ms)
{
    memset(event, 0, sizeof(*event));
    while (!take_msg(conn, &event->msg))
    {
        int got = fill(conn, timeout_ms);
        if (got <= 0)
            return got;
    }
    event->type = event->msg.type;
    event->window = find_window(conn, event->msg.window);
    if (event->type == HEIM_EV_FRAME && event->window)
        event->window->busy = false;
    if (event->type == HEIM_EV_CLIPBOARD)
        read_clipboard(conn, event);
    return 1;
}

heim_conn_t *heim_connect(void)
{
    heim_conn_t *conn = calloc(1, sizeof(*conn));
    if (!conn)
        return NULL;
    conn->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, HEIM_SOCKET);
    if (conn->fd < 0 || connect(conn->fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        if (conn->fd >= 0)
            close(conn->fd);
        free(conn);
        return NULL;
    }

    heim_msg_t hello;
    memset(&hello, 0, sizeof(hello));
    hello.type = HEIM_REQ_HELLO;
    hello.hello.version = HEIM_PROTOCOL_VERSION;
    heim_msg_t welcome;
    if (!send_msg(conn, &hello, -1))
        goto fail;
    // the welcome is the first event
    while (!take_msg(conn, &welcome))
        if (fill(conn, 5000) <= 0)
            goto fail;
    if (welcome.type != HEIM_EV_WELCOME)
        goto fail;
    conn->screen_width = welcome.size.width;
    conn->screen_height = welcome.size.height;
    conn->next_id = 1;
    return conn;

fail:
    close(conn->fd);
    free(conn);
    errno = EPROTONOSUPPORT;
    return NULL;
}

void heim_disconnect(heim_conn_t *conn)
{
    if (!conn)
        return;
    while (conn->windows)
    {
        heim_window_t *w = conn->windows;
        conn->windows = w->next;
        if (w->map)
            munmap(w->map, w->map_size);
        free(w);
    }
    for (int i = 0; i < conn->fd_count; i++)
        close(conn->fds[i]);
    close(conn->fd);
    free(conn->clipboard);
    free(conn);
}

int heim_fd(const heim_conn_t *conn)
{
    return conn->fd;
}

void heim_screen_size(const heim_conn_t *conn, int32_t *width, int32_t *height)
{
    *width = conn->screen_width;
    *height = conn->screen_height;
}

bool heim_window_resize(heim_window_t *win, int32_t width, int32_t height)
{
    if (width <= 0 || height <= 0 || width > HEIM_SIZE_MAX || height > HEIM_SIZE_MAX)
        return false;
    size_t size = (size_t)width * (size_t)height * 4;
    size = (size + 4095) & ~(size_t)4095;
    int fd = shm_create(size, FD_CLOEXEC);
    if (fd < 0)
        return false;
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
    {
        close(fd);
        return false;
    }

    heim_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = HEIM_REQ_ATTACH;
    msg.window = win->id;
    msg.attach.width = width;
    msg.attach.height = height;
    msg.attach.stride = width;
    bool sent = send_msg(win->conn, &msg, fd);
    close(fd); // the server has its own reference now
    if (!sent)
    {
        munmap(map, size);
        return false;
    }

    if (win->map)
        munmap(win->map, win->map_size);
    win->map = map;
    win->map_size = size;
    gfx_buffer_init(&win->buffer, map, width, height, width);
    return true;
}

static heim_window_t *new_window(heim_conn_t *conn, int32_t width, int32_t height,
                                 const char *title, uint32_t flags, uint32_t parent)
{
    heim_window_t *win = calloc(1, sizeof(*win));
    if (!win)
        return NULL;
    win->conn = conn;
    win->id = conn->next_id++;

    heim_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = HEIM_REQ_CREATE;
    msg.window = win->id;
    msg.create.width = width;
    msg.create.height = height;
    msg.create.flags = flags;
    msg.create.parent = parent;
    copy_title(msg.create.title, title);
    if (!send_msg(conn, &msg, -1) || !heim_window_resize(win, width, height))
    {
        free(win);
        return NULL;
    }
    win->next = conn->windows;
    conn->windows = win;
    return win;
}

heim_window_t *heim_window_new(heim_conn_t *conn, int32_t width, int32_t height,
                               const char *title, uint32_t flags)
{
    return new_window(conn, width, height, title, flags & ~(uint32_t)HEIM_WINDOW_POPUP, 0);
}

heim_window_t *heim_popup_new(heim_window_t *parent, int32_t width, int32_t height,
                              uint32_t flags)
{
    return new_window(parent->conn, width, height, "",
                  (flags & HEIM_WINDOW_TOOLTIP) | HEIM_WINDOW_POPUP, parent->id);
}

void heim_window_place(heim_window_t *popup, gfx_rect_t r, uint32_t side, int32_t margin)
{
    heim_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = HEIM_REQ_PLACE;
    msg.window = popup->id;
    msg.place.x = r.x;
    msg.place.y = r.y;
    msg.place.width = r.w;
    msg.place.height = r.h;
    msg.place.side = side;
    msg.place.margin = margin;
    send_msg(popup->conn, &msg, -1);
}

void heim_window_free(heim_window_t *win)
{
    if (!win)
        return;
    heim_conn_t *conn = win->conn;
    heim_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = HEIM_REQ_DESTROY;
    msg.window = win->id;
    send_msg(conn, &msg, -1);

    heim_window_t **link = &conn->windows;
    while (*link && *link != win)
        link = &(*link)->next;
    if (*link)
        *link = win->next;
    if (win->map)
        munmap(win->map, win->map_size);
    free(win);
}

gfx_buffer_t *heim_window_buffer(heim_window_t *win)
{
    return &win->buffer;
}

void heim_window_commit(heim_window_t *win, gfx_rect_t damage)
{
    damage = gfx_rect_intersect(damage, gfx_rect(0, 0, win->buffer.width, win->buffer.height));
    heim_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = HEIM_REQ_COMMIT;
    msg.window = win->id;
    msg.rect.x = damage.x;
    msg.rect.y = damage.y;
    msg.rect.width = damage.w;
    msg.rect.height = damage.h;
    if (send_msg(win->conn, &msg, -1))
        win->busy = true;
}

bool heim_window_busy(const heim_window_t *win)
{
    return win->busy;
}

void heim_window_set_title(heim_window_t *win, const char *title)
{
    heim_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = HEIM_REQ_TITLE;
    msg.window = win->id;
    copy_title(msg.title.title, title);
    send_msg(win->conn, &msg, -1);
}

void heim_window_set_cursor(heim_window_t *win, uint32_t shape)
{
    heim_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = HEIM_REQ_CURSOR;
    msg.window = win->id;
    msg.cursor.shape = shape;
    send_msg(win->conn, &msg, -1);
}

void heim_window_set_data(heim_window_t *win, void *data)
{
    win->data = data;
}

void *heim_window_data(const heim_window_t *win)
{
    return win->data;
}

void heim_clipboard_set(heim_conn_t *conn, const char *text, size_t length)
{
    heim_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = HEIM_REQ_CLIP_SET;
    if (length == 0 || length > HEIM_CLIPBOARD_MAX)
    {
        send_msg(conn, &msg, -1);
        return;
    }
    int fd = shm_create(length, FD_CLOEXEC);
    void *map = fd >= 0 ? mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)
                        : MAP_FAILED;
    if (map == MAP_FAILED)
    {
        if (fd >= 0)
            close(fd);
        return;
    }
    memcpy(map, text, length);
    munmap(map, length);
    msg.clip.length = (uint32_t)length;
    send_msg(conn, &msg, fd);
    close(fd);
}

void heim_clipboard_request(heim_conn_t *conn)
{
    heim_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = HEIM_REQ_CLIP_GET;
    send_msg(conn, &msg, -1);
}

void heim_settings_changed(heim_conn_t *conn, const char *name)
{
    heim_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = HEIM_REQ_SETTINGS;
    snprintf(msg.settings.name, sizeof(msg.settings.name), "%s", name);
    send_msg(conn, &msg, -1);
}
