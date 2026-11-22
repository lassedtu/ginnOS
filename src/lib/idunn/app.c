/**
 * @file app.c
 * @brief idunn: the app loop and windows on the window server.
 *
 * a window is drawn only when its last frame is on the screen (after
 * HEIM_EV_FRAME), so it is never drawn twice before it shows, and a
 * transition draws one step per frame.
 */

#include "idunn_internal.h"

#include <errno.h>
#include <heimdall.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/input.h>
#include <time.h>

#define FONT_DIR "/usr/share/fonts"

struct idunn_app
{
    heim_conn_t *conn;
    idunn_context_t *ctx;
    idunn_window_t *windows;
    idunn_window_t *paste_window; // it asked for the clipboard's text
    bool quit;
    char name[HEIM_APP_ID_MAX]; // the program's id, for the shell

    void (*on_event)(idunn_app_t *app, const heim_msg_t *msg, void *data);
    void *on_event_data;
    void (*every)(void *data);
    void *every_data;
    uint32_t every_period; // ms (0: off)
    uint32_t every_next;   // when it is called next

    struct
    {
        int fd;
        void (*ready)(int fd, void *data);
        void *data;
    } watch[IDUNN_WATCH_MAX]; // fds the loop also waits on (a pty)
    int watch_count;
};

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/**
 * set the scale of a theme for the screen (when the scale is auto).
 * @return true if it changed.
 */
static bool fit(idunn_app_t *app, idunn_theme_t *theme)
{
    int32_t width;
    int32_t height;
    heim_screen_size(app->conn, &width, &height);
    return idunn_theme_fit(theme, width);
}

idunn_app_t *idunn_app_new(const char *name)
{
    idunn_app_t *app = calloc(1, sizeof(*app));
    if (!app)
        return NULL;
    snprintf(app->name, sizeof(app->name), "%s", name ? name : "");
    idunn_theme_t theme;
    idunn_theme_load(&theme);
    app->ctx = idunn_context_new(&theme, FONT_DIR, ICON_DIR "lucide.pack");
    app->conn = app->ctx ? heim_connect() : NULL;
    if (!app->conn)
    {
        idunn_context_free(app->ctx);
        free(app);
        return NULL;
    }
    // the scale can follow the size of the screen
    if (fit(app, &theme))
        idunn_context_set_theme(app->ctx, &theme);
    return app;
}

void idunn_app_quit(idunn_app_t *app)
{
    app->quit = true;
}

heim_conn_t *idunn_app_conn(const idunn_app_t *app)
{
    return app->conn;
}

void idunn_app_on_event(idunn_app_t *app,
                        void (*on_event)(idunn_app_t *app, const heim_msg_t *msg, void *data),
                        void *data)
{
    app->on_event = on_event;
    app->on_event_data = data;
}

void idunn_app_every(idunn_app_t *app, uint32_t period, void (*every)(void *data), void *data)
{
    app->every = every;
    app->every_data = data;
    app->every_period = every ? period : 0;
    app->every_next = now_ms() + period;
}

void idunn_app_watch(idunn_app_t *app, int fd, void (*ready)(int fd, void *data), void *data)
{
    for (int i = 0; i < app->watch_count; i++)
    {
        if (app->watch[i].fd != fd)
            continue;
        if (ready)
        {
            app->watch[i].ready = ready;
            app->watch[i].data = data;
        }
        else
        {
            app->watch[i] = app->watch[--app->watch_count];
        }
        return;
    }
    if (!ready || app->watch_count == IDUNN_WATCH_MAX)
        return;
    app->watch[app->watch_count].fd = fd;
    app->watch[app->watch_count].ready = ready;
    app->watch[app->watch_count].data = data;
    app->watch_count++;
}

/**
 * wait for the server or a watched fd, and call the watches that are
 * ready.
 * @return 1 if the server has a message, 0 if not (a timer or a watch),
 *         -1 if the wait failed.
 */
static int wait_watches(idunn_app_t *app, int timeout)
{
    struct pollfd fds[1 + IDUNN_WATCH_MAX];
    int count = app->watch_count;
    fds[0] = (struct pollfd){heim_fd(app->conn), POLLIN, 0};
    for (int i = 0; i < count; i++)
        fds[1 + i] = (struct pollfd){app->watch[i].fd, POLLIN, 0};
    int ready = poll(fds, (nfds_t)(1 + count), timeout);
    if (ready < 0)
        return errno == EINTR ? 0 : -1;
    for (int i = 0; i < count; i++)
    {
        if (!fds[1 + i].revents)
            continue;
        // a callback can stop a watch: find it again by its fd
        for (int j = 0; j < app->watch_count; j++)
        {
            if (app->watch[j].fd == fds[1 + i].fd)
            {
                app->watch[j].ready(app->watch[j].fd, app->watch[j].data);
                break;
            }
        }
    }
    return fds[0].revents ? 1 : 0;
}

void idunn_window_set_title(idunn_window_t *win, const char *title)
{
    if (win && win->heim)
        heim_window_set_title(win->heim, title);
}

const idunn_theme_t *idunn_app_theme(const idunn_app_t *app)
{
    return &app->ctx->theme;
}

/**
 * a window of another size: a new buffer, and the server shows it at that
 * size.
 */
static void resize(idunn_window_t *win, int32_t width, int32_t height)
{
    if (heim_window_resize(win->heim, width, height))
        idunn_window_use_buffer(win, heim_window_buffer(win->heim));
}

void idunn_app_set_theme(idunn_app_t *app, const idunn_theme_t *theme)
{
    int32_t from = app->ctx->theme.scale;
    int32_t to = theme->scale;
    idunn_context_set_theme(app->ctx, theme);
    for (idunn_window_t *win = app->windows; win; win = win->next)
    {
        if (from != to && win->opener && !win->dead)
        {
            // a popup is placed for the old scale
            idunn_popup_close(win);
            continue;
        }
        if (from != to && !win->dead && win->buffer)
            resize(win, win->buffer->width * to / from, win->buffer->height * to / from);
        win->needs_layout = true;
        idunn_window_invalidate(win);
    }
}

bool idunn_app_save_theme(idunn_app_t *app, const char *key, const char *value)
{
    conf_t *conf = conf_open("theme");
    if (!conf || conf_set(conf, "theme", key, value) != 0)
    {
        conf_free(conf);
        return false;
    }
    bool saved = conf_save(conf) == 0;
    if (saved)
    {
        // every app and the server load it again, this one too
        heim_settings_changed(app->conn, "theme");
    }
    else
    {
        // it cannot be saved: this app shows it at least
        idunn_theme_t theme;
        idunn_theme_from_conf(&theme, conf);
        idunn_app_set_theme(app, &theme);
    }
    conf_free(conf);
    return saved;
}

static void clipboard_set(idunn_window_t *win, const char *text, size_t length)
{
    heim_clipboard_set(win->app->conn, text, length);
}

static void clipboard_get(idunn_window_t *win)
{
    win->app->paste_window = win;
    heim_clipboard_request(win->app->conn);
}

static idunn_window_t *popup_new(idunn_window_t *opener, int32_t width, int32_t height,
                                 bool tooltip);
static void popup_place(idunn_window_t *popup);
static void popup_free(idunn_window_t *popup);

/** an idunn window over a heimdall window, in the app's list */
static idunn_window_t *wrap(idunn_app_t *app, heim_window_t *heim)
{
    idunn_window_t *win = calloc(1, sizeof(*win));
    if (!win)
    {
        heim_window_free(heim);
        return NULL;
    }
    heim_window_set_data(heim, win);
    win->ctx = app->ctx;
    win->app = app;
    win->heim = heim;
    win->buffer = heim_window_buffer(heim);
    win->needs_layout = true;
    win->clipboard_set = clipboard_set;
    win->clipboard_get = clipboard_get;
    win->popup_new = popup_new;
    win->popup_place = popup_place;
    win->popup_free = popup_free;
    win->next = app->windows;
    app->windows = win;
    return win;
}

idunn_window_t *idunn_window_new(idunn_app_t *app, const char *title, int32_t width,
                                 int32_t height)
{
    const idunn_theme_t *t = &app->ctx->theme;
    heim_window_t *heim = heim_window_new(app->conn, idunn_px(t, width), idunn_px(t, height),
                                          title, HEIM_WINDOW_RESIZABLE);
    if (heim && app->name[0])
        heim_window_set_app(heim, app->name);
    return heim ? wrap(app, heim) : NULL;
}

idunn_window_t *idunn_panel_new(idunn_app_t *app, int32_t height)
{
    // the server makes it as wide as the screen
    int32_t width;
    int32_t screen_height;
    heim_screen_size(app->conn, &width, &screen_height);
    const idunn_theme_t *t = &app->ctx->theme;
    heim_window_t *heim = heim_window_new(app->conn, width, idunn_px(t, height), "",
                                          HEIM_WINDOW_PANEL);
    return heim ? wrap(app, heim) : NULL;
}

static idunn_window_t *popup_new(idunn_window_t *opener, int32_t width, int32_t height,
                                 bool tooltip)
{
    heim_window_t *heim =
        heim_popup_new(opener->heim, width, height, tooltip ? HEIM_WINDOW_TOOLTIP : 0);
    return heim ? wrap(opener->app, heim) : NULL;
}

static void popup_place(idunn_window_t *popup)
{
    heim_window_place(popup->heim, popup->anchor, (uint32_t)popup->side, popup->margin);
}

static void popup_free(idunn_window_t *popup)
{
    idunn_window_close(popup);
}

void idunn_window_close(idunn_window_t *win)
{
    idunn_app_t *app = win->app;
    if (app->paste_window == win)
        app->paste_window = NULL;
    for (idunn_window_t **p = &app->windows; *p; p = &(*p)->next)
    {
        if (*p == win)
        {
            *p = win->next;
            break;
        }
    }
    idunn_window_free_widgets(win);
    heim_window_free(win->heim);
    free(win);
}

/**
 * draw what changed and show it, unless the last frame is not on the
 * screen yet (HEIM_EV_FRAME draws then).
 */
static void present(idunn_window_t *win, uint32_t now)
{
    heim_window_t *heim = win->heim;
    // timers fire now; what they change is drawn with the next frame
    idunn_window_tick(win, now);
    if (heim_window_busy(heim))
        return;
    // each part apart, so two far apart changes do not send all between
    gfx_rect_t parts[IDUNN_DAMAGE_MAX + 1];
    int count = idunn_window_draw_parts(win, parts);
    for (int i = 0; i < count; i++)
        heim_window_commit(heim, parts[i]);
}

static void pointer(idunn_window_t *win, const heim_event_t *ev)
{
    const typeof(ev->msg.pointer) *p = &ev->msg.pointer;
    uint32_t cursor = win->cursor;
    switch (p->kind)
    {
    case HEIM_POINTER_MOTION:
    case HEIM_POINTER_ENTER:
        idunn_window_pointer(win, IDUNN_POINTER_MOTION, p->x, p->y, p->mods);
        break;
    case HEIM_POINTER_LEAVE:
        idunn_window_pointer(win, IDUNN_POINTER_LEAVE, p->x, p->y, p->mods);
        break;
    case HEIM_POINTER_PRESS:
    case HEIM_POINTER_RELEASE:
        if (p->button == BTN_LEFT)
            idunn_window_pointer(win,
                                 p->kind == HEIM_POINTER_PRESS ? IDUNN_POINTER_PRESS
                                                               : IDUNN_POINTER_RELEASE,
                                 p->x, p->y, p->mods);
        break;
    case HEIM_POINTER_SCROLL:
        idunn_window_scroll(win, p->x, p->y, p->value);
        break;
    default:
        break;
    }
    if (win->cursor != cursor)
        heim_window_set_cursor(win->heim, win->cursor);
}

int idunn_app_run(idunn_app_t *app)
{
    heim_event_t ev;
    while (app->windows && !app->quit)
    {
        idunn_reap(app->ctx);
        uint32_t now = now_ms();
        int timeout = -1;
        for (idunn_window_t *win = app->windows; win; win = win->next)
        {
            win->now = now;
            if (win->dead)
                continue;
            present(win, now);
            // wait no longer than the next timer (a caret blinks)
            uint32_t wake = idunn_window_next_wake(win);
            if (wake)
            {
                int32_t left = (int32_t)(wake - now);
                left = left < 0 ? 0 : left;
                if (timeout < 0 || left < timeout)
                    timeout = left;
            }
        }
        // a window made while drawing (a tooltip) or changed by a timer
        // is drawn before the loop waits
        for (idunn_window_t *win = app->windows; win; win = win->next)
        {
            bool work = win->needs_layout || !gfx_rect_empty(win->damage);
            if (!win->dead && work && !heim_window_busy(win->heim))
                timeout = 0;
        }
        // the app's own timer (a clock)
        if (app->every_period)
        {
            int32_t left = (int32_t)(app->every_next - now);
            if (left <= 0)
            {
                app->every_next = now + app->every_period;
                app->every(app->every_data);
                timeout = 0; // draw what it changed
            }
            else if (timeout < 0 || left < timeout)
            {
                timeout = left;
            }
        }
        if (app->watch_count && !heim_pending(app->conn))
        {
            int server = wait_watches(app, timeout);
            if (server < 0)
                break;
            if (server == 0)
                continue; // a watch or a timer: draw what changed
            timeout = 0;
        }
        int got = heim_next_event(app->conn, &ev, timeout);
        if (got < 0)
            break;
        if (got == 0)
            continue; // a timer: present ticks the windows
        idunn_window_t *win = ev.window ? heim_window_data(ev.window) : NULL;
        if (win && win->dead && ev.type != HEIM_EV_CLOSE)
            continue;
        if (win)
            win->now = now_ms();
        switch (ev.type)
        {
        case HEIM_EV_CLOSE:
            // the server closes popups (a press outside them); the app, windows
            if (win && win->opener)
                idunn_popup_close(win);
            else if (win && !win->dead)
                idunn_window_close(win);
            break;
        case HEIM_EV_CONFIGURE:
            if (win)
                resize(win, ev.msg.configure.width, ev.msg.configure.height);
            break;
        case HEIM_EV_SCREEN:
        {
            idunn_theme_t theme = app->ctx->theme;
            if (fit(app, &theme))
                idunn_app_set_theme(app, &theme);
            break;
        }
        case HEIM_EV_POINTER:
            if (win)
                pointer(win, &ev);
            break;
        case HEIM_EV_KEY:
            if (win && ev.msg.key.value != HEIM_KEY_RELEASE)
                idunn_window_key(win, ev.msg.key.code, ev.msg.key.mods, ev.msg.key.text);
            break;
        case HEIM_EV_CLIPBOARD:
            if (app->paste_window)
            {
                idunn_window_t *to = app->paste_window;
                app->paste_window = NULL;
                to->now = now_ms();
                idunn_window_paste(to, ev.clipboard ? ev.clipboard : "", ev.clipboard_length);
            }
            break;
        case HEIM_EV_SETTINGS:
            if (strcmp(ev.msg.settings.name, "theme") == 0)
            {
                idunn_theme_t theme;
                idunn_theme_load(&theme);
                fit(app, &theme);
                idunn_app_set_theme(app, &theme);
            }
            // the program's own settings (and the theme, if it cares)
            if (app->on_event)
                app->on_event(app, &ev.msg, app->on_event_data);
            break;
        default:
            if (!win && app->on_event)
                app->on_event(app, &ev.msg, app->on_event_data);
            break;
        }
    }
    while (app->windows)
        idunn_window_close(app->windows);
    heim_disconnect(app->conn);
    idunn_context_free(app->ctx);
    free(app);
    return 0;
}
