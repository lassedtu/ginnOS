/**
 * @file client.c
 * @brief the clients: their sockets and their requests.
 *
 * the sockets are non-blocking. what a client sends is read into its
 * buffer and handled one 64-byte message at a time; files that come with
 * the messages wait in a queue for the message that uses them. what the
 * server sends waits in a queue until the socket takes it. a client that
 * breaks the protocol, or reads so slowly that its queue fills, is
 * disconnected.
 */

#include "heimdalld.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define MSG_SIZE ((int)sizeof(heim_msg_t))

void client_accept(int listen_fd)
{
    int fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0)
        return;
    client_t *c = calloc(1, sizeof(*c));
    if (!c)
    {
        close(fd);
        return;
    }
    c->fd = fd;
    c->next = S.clients;
    S.clients = c;
}

static void kill_client(client_t *c, const char *why)
{
    if (!c->dead)
        printf("heimdalld: a client is disconnected: %s\n", why);
    c->dead = true;
}

void client_send(client_t *c, const heim_msg_t *msg, int fd)
{
    if (c->dead)
    {
        if (fd >= 0)
            close(fd);
        return;
    }
    if (c->out_count == CLIENT_OUT)
    {
        if (fd >= 0)
            close(fd);
        kill_client(c, "it does not read its events");
        return;
    }
    int slot = (c->out_head + c->out_count) % CLIENT_OUT;
    c->out[slot] = *msg;
    c->out_fd[slot] = fd;
    c->out_count++;
    client_flush(c);
}

void client_send_window(window_t *w, heim_msg_t *msg)
{
    msg->window = w->id;
    client_send(w->client, msg, -1);
}

void client_flush(client_t *c)
{
    while (c->out_count > 0 && !c->dead)
    {
        int slot = c->out_head;
        const uint8_t *bytes = (const uint8_t *)&c->out[slot];
        struct iovec iov = {(void *)(bytes + c->out_done), MSG_SIZE - c->out_done};
        union
        {
            struct cmsghdr header;
            uint8_t space[CMSG_SPACE(sizeof(int))];
        } control;
        struct msghdr mh;
        memset(&mh, 0, sizeof(mh));
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        int fd = c->out_fd[slot];
        if (fd >= 0 && c->out_done == 0)
        {
            memset(&control, 0, sizeof(control));
            mh.msg_control = &control;
            mh.msg_controllen = CMSG_SPACE(sizeof(int));
            struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
            cm->cmsg_level = SOL_SOCKET;
            cm->cmsg_type = SCM_RIGHTS;
            cm->cmsg_len = CMSG_LEN(sizeof(int));
            memcpy(CMSG_DATA(cm), &fd, sizeof(int));
        }
        ssize_t n = sendmsg(c->fd, &mh, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n < 0)
        {
            if (errno != EAGAIN && errno != EINTR)
                kill_client(c, "it closed its socket");
            return;
        }
        if (fd >= 0 && c->out_done == 0)
        {
            close(fd); // the client has it now
            c->out_fd[slot] = -1;
        }
        c->out_done += (size_t)n;
        if (c->out_done == (size_t)MSG_SIZE)
        {
            c->out_done = 0;
            c->out_head = (c->out_head + 1) % CLIENT_OUT;
            c->out_count--;
        }
    }
}

static int take_fd(client_t *c)
{
    if (c->fd_count == 0)
        return -1;
    int fd = c->fds[0];
    memmove(c->fds, c->fds + 1, (size_t)(c->fd_count - 1) * sizeof(int));
    c->fd_count--;
    return fd;
}

static bool good_size(int32_t w, int32_t h)
{
    return w > 0 && h > 0 && w <= HEIM_SIZE_MAX && h <= HEIM_SIZE_MAX;
}

static void attach(client_t *c, window_t *w, const heim_msg_t *msg)
{
    int fd = take_fd(c);
    int32_t width = msg->attach.width;
    int32_t height = msg->attach.height;
    int32_t stride = msg->attach.stride;
    if (fd < 0 || !good_size(width, height) || stride < width || stride > HEIM_SIZE_MAX)
    {
        if (fd >= 0)
            close(fd);
        kill_client(c, "a bad buffer");
        return;
    }
    size_t size = (size_t)stride * (size_t)height * 4;
    struct stat st;
    void *map = MAP_FAILED;
    if (fstat(fd, &st) == 0 && (size_t)st.st_size >= size)
        map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED)
    {
        kill_client(c, "a buffer smaller than it says");
        return;
    }
    if (w->pending_map)
        munmap(w->pending_map, w->pending_size);
    w->pending_map = map;
    w->pending_size = size;
    gfx_buffer_init(&w->pending, map, width, height, stride);
}

static void commit(client_t *c, window_t *w, const heim_msg_t *msg)
{
    gfx_rect_t changed = gfx_rect(msg->rect.x, msg->rect.y, msg->rect.width, msg->rect.height);
    if (w->pending_map)
    {
        // a new buffer: from now on the window has its size
        gfx_buffer_t *own = gfx_buffer_new(w->pending.width, w->pending.height);
        if (!own)
        {
            kill_client(c, "out of memory");
            return;
        }
        window_damage(w);
        if (w->map)
            munmap(w->map, w->map_size);
        gfx_buffer_free(w->own);
        int32_t old_width = w->content.width;
        w->own = own;
        w->content = *own;
        w->content.owned = false;
        w->shared = w->pending;
        w->map = w->pending_map;
        w->map_size = w->pending_size;
        w->pending_map = NULL;
        if (old_width != w->content.width)
            w->bar_dirty = true;
        changed = gfx_rect(0, 0, own->width, own->height); // all of it
        window_damage(w);
    }
    if (!w->own)
    {
        kill_client(c, "a commit without a buffer");
        return;
    }
    changed = gfx_rect_intersect(changed, gfx_rect(0, 0, w->own->width, w->own->height));
    gfx_copy(w->own, changed.x, changed.y, &w->shared, changed);
    if (!w->shown)
    {
        window_show(w);
        input_pointer_update();
    }
    else
    {
        gfx_rect_t cr = window_content(w);
        gfx_rect_t d = gfx_rect(cr.x + changed.x, cr.y + changed.y, changed.w, changed.h);
        if (window_visible(w))
            screen_damage(gfx_rect_intersect(d, cr));
    }
    w->frame_pending = true;
}

static void clip_set(client_t *c, const heim_msg_t *msg)
{
    uint32_t length = msg->clip.length;
    free(S.clip);
    S.clip = NULL;
    S.clip_length = 0;
    if (length == 0)
        return;
    int fd = take_fd(c);
    if (fd < 0 || length > HEIM_CLIPBOARD_MAX)
    {
        if (fd >= 0)
            close(fd);
        kill_client(c, "a bad clipboard");
        return;
    }
    struct stat st;
    void *map = MAP_FAILED;
    if (fstat(fd, &st) == 0 && (uint32_t)st.st_size >= length)
        map = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED)
    {
        kill_client(c, "a bad clipboard");
        return;
    }
    S.clip = malloc(length);
    if (S.clip)
    {
        memcpy(S.clip, map, length);
        S.clip_length = length;
    }
    munmap(map, length);
}

static void clip_get(client_t *c)
{
    heim_msg_t msg = {0};
    msg.type = HEIM_EV_CLIPBOARD;
    int fd = -1;
    if (S.clip_length)
    {
        fd = shm_create(S.clip_length, FD_CLOEXEC);
        void *map = fd >= 0 ? mmap(NULL, S.clip_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)
                            : MAP_FAILED;
        if (map != MAP_FAILED)
        {
            memcpy(map, S.clip, S.clip_length);
            munmap(map, S.clip_length);
            msg.clip.length = S.clip_length;
        }
        else if (fd >= 0)
        {
            close(fd);
            fd = -1;
        }
    }
    client_send(c, &msg, fd);
}

/**
 * a settings file changed: tell every client (the one that changed it
 * too). a name that libconf would not open is a protocol error.
 */
static void settings_changed(client_t *c, heim_msg_t *msg)
{
    char *name = msg->settings.name;
    name[HEIM_SETTINGS_MAX - 1] = '\0';
    bool good = name[0] != '\0';
    for (const char *p = name; *p; p++)
        good = good && (isalnum((unsigned char)*p) || *p == '-' || *p == '_');
    if (!good)
    {
        kill_client(c, "a bad settings name");
        return;
    }
    // the decorations follow the theme too
    if (strcmp(name, "theme") == 0)
    {
        idunn_theme_load(&S.theme);
        window_look();
    }
    heim_msg_t event = {0};
    event.type = HEIM_EV_SETTINGS;
    memcpy(event.settings.name, name, sizeof(event.settings.name));
    // a client that is killed here (its queue is full) stays on the list
    // until client_reap, so the walk is safe
    for (client_t *other = S.clients; other; other = other->next)
    {
        if (other->hello)
            client_send(other, &event, -1);
    }
}

static void handle(client_t *c, heim_msg_t *msg)
{
    if (!c->hello)
    {
        if (msg->type != HEIM_REQ_HELLO || msg->hello.version != HEIM_PROTOCOL_VERSION)
        {
            kill_client(c, "no hello, or another protocol version");
            return;
        }
        c->hello = true;
        heim_msg_t welcome = {0};
        welcome.type = HEIM_EV_WELCOME;
        welcome.size.width = S.width;
        welcome.size.height = S.height;
        client_send(c, &welcome, -1);
        return;
    }

    window_t *w = msg->window ? window_find(c, msg->window) : NULL;
    switch (msg->type)
    {
    case HEIM_REQ_CREATE:
    {
        msg->create.title[HEIM_TITLE_MAX - 1] = '\0';
        window_t *parent = NULL;
        if (msg->create.flags & HEIM_WINDOW_POPUP)
        {
            parent = window_find(c, msg->create.parent);
            if (!parent)
            {
                kill_client(c, "a popup without its window");
                return;
            }
        }
        if (msg->window == 0 || w || !good_size(msg->create.width, msg->create.height))
            kill_client(c, "a bad window");
        else if (!window_new(c, msg->window, msg->create.flags, msg->create.title, parent))
            kill_client(c, "out of memory");
        return;
    }
    case HEIM_REQ_CLIP_SET:
        clip_set(c, msg);
        return;
    case HEIM_REQ_CLIP_GET:
        clip_get(c);
        return;
    case HEIM_REQ_SETTINGS:
        settings_changed(c, msg);
        return;
    default:
        break;
    }

    if (!w)
    {
        kill_client(c, "a request for a window it does not have");
        return;
    }
    switch (msg->type)
    {
    case HEIM_REQ_ATTACH:
        attach(c, w, msg);
        break;
    case HEIM_REQ_COMMIT:
        commit(c, w, msg);
        break;
    case HEIM_REQ_PLACE:
        w->anchor = gfx_rect(msg->place.x, msg->place.y, msg->place.width, msg->place.height);
        w->side = msg->place.side;
        w->margin = msg->place.margin > 0 && msg->place.margin < 256 ? msg->place.margin : 0;
        if (w->shown)
            window_place(w);
        break;
    case HEIM_REQ_TITLE:
        memcpy(w->title, msg->title.title, HEIM_TITLE_MAX);
        w->title[HEIM_TITLE_MAX - 1] = '\0';
        w->bar_dirty = true;
        if (window_visible(w) && window_decorated(w))
            screen_damage(gfx_rect(w->x, w->y, w->content.width, TITLE_HEIGHT));
        break;
    case HEIM_REQ_CURSOR:
        w->cursor = msg->cursor.shape <= HEIM_CURSOR_HAND ? msg->cursor.shape : HEIM_CURSOR_ARROW;
        input_pointer_update();
        break;
    case HEIM_REQ_DESTROY:
        window_destroy(w);
        break;
    default:
        kill_client(c, "an unknown request");
        break;
    }
}

void client_read(client_t *c)
{
    while (!c->dead)
    {
        struct iovec iov = {c->in + c->in_used, (size_t)(CLIENT_IN - c->in_used)};
        union
        {
            struct cmsghdr header;
            uint8_t space[CMSG_SPACE(sizeof(int) * CLIENT_FDS)];
        } control;
        struct msghdr mh;
        memset(&mh, 0, sizeof(mh));
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = &control;
        mh.msg_controllen = sizeof(control);
        ssize_t n = recvmsg(c->fd, &mh, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        if (n <= 0)
        {
            c->dead = true; // it went away
            return;
        }
        c->in_used += (int)n;

        for (struct cmsghdr *cm = CMSG_FIRSTHDR(&mh); cm; cm = CMSG_NXTHDR(&mh, cm))
        {
            if (cm->cmsg_level != SOL_SOCKET || cm->cmsg_type != SCM_RIGHTS)
                continue;
            int count = (int)((cm->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            for (int i = 0; i < count; i++)
            {
                int fd;
                memcpy(&fd, CMSG_DATA(cm) + i * sizeof(int), sizeof(int));
                if (c->fd_count < CLIENT_FDS)
                    c->fds[c->fd_count++] = fd;
                else
                {
                    close(fd);
                    kill_client(c, "too many files");
                }
            }
        }

        int at = 0;
        while (c->in_used - at >= MSG_SIZE && !c->dead)
        {
            heim_msg_t msg;
            memcpy(&msg, c->in + at, MSG_SIZE);
            at += MSG_SIZE;
            handle(c, &msg);
        }
        memmove(c->in, c->in + at, (size_t)(c->in_used - at));
        c->in_used -= at;
    }
}

/**
 * free the clients that went away, with their windows.
 */
void client_reap(void)
{
    client_t **link = &S.clients;
    while (*link)
    {
        client_t *c = *link;
        if (!c->dead)
        {
            link = &c->next;
            continue;
        }
        *link = c->next;
        window_t *w = S.bottom;
        while (w)
        {
            window_t *next = w->above;
            if (w->client == c)
                window_destroy(w);
            w = next;
        }
        for (int i = 0; i < c->fd_count; i++)
            close(c->fds[i]);
        for (int i = 0; i < c->out_count; i++)
        {
            int fd = c->out_fd[(c->out_head + i) % CLIENT_OUT];
            if (fd >= 0)
                close(fd);
        }
        close(c->fd);
        free(c);
    }
}
