/**
 * @file app.c
 * @brief idunn: the app loop and windows on the window server.
 *
 * a window is drawn only when its last frame is on the screen (after
 * HEIM_EV_FRAME), so it is never drawn twice before it shows, and a
 * transition draws one step per frame.
 */

#include "idunn_internal.h"

#include <heimdall.h>
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
};

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

idunn_app_t *idunn_app_new(const char *name)
{
    (void)name;
    idunn_app_t *app = calloc(1, sizeof(*app));
    if (!app)
        return NULL;
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
    return app;
}

void idunn_app_quit(idunn_app_t *app)
{
    app->quit = true;
}

const idunn_theme_t *idunn_app_theme(const idunn_app_t *app)
{
    return &app->ctx->theme;
}

void idunn_app_set_theme(idunn_app_t *app, const idunn_theme_t *theme)
{
    idunn_context_set_theme(app->ctx, theme);
    for (idunn_window_t *win = app->windows; win; win = win->next)
    {
        win->needs_layout = true;
        idunn_window_invalidate(win);
    }
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
    gfx_rect_t damage = idunn_window_draw(win);
    if (!gfx_rect_empty(damage))
        heim_window_commit(heim, damage);
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
            if (win && heim_window_resize(win->heim, ev.msg.configure.width,
                                          ev.msg.configure.height))
                idunn_window_use_buffer(win, heim_window_buffer(win->heim));
            break;
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
                idunn_app_set_theme(app, &theme);
            }
            break;
        default:
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
