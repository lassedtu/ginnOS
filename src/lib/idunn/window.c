/**
 * @file window.c
 * @brief idunn: a window's widgets: layout, drawing what changed, and
 *        the pointer and the keys. no window server here (app.c has it),
 *        so the host tests can drive a window.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define HOVER_MS        100
#define DOUBLE_CLICK_MS 500
#define DOUBLE_CLICK_PX 4

idunn_window_t *idunn_window_offscreen(idunn_context_t *ctx, int32_t width, int32_t height)
{
    idunn_window_t *win = calloc(1, sizeof(*win));
    if (!win)
        return NULL;
    win->ctx = ctx;
    win->buffer = gfx_buffer_new(width, height);
    if (!win->buffer)
    {
        free(win);
        return NULL;
    }
    win->own_buffer = true;
    win->needs_layout = true;
    return win;
}

void idunn_window_free_widgets(idunn_window_t *win)
{
    idunn_widget_free(win->root);
    win->root = NULL;
    free(win->clipboard);
    win->clipboard = NULL;
    if (win->own_buffer)
        gfx_buffer_free(win->buffer);
    win->buffer = NULL;
}

void idunn_window_set_root(idunn_window_t *win, idunn_widget_t *root)
{
    if (win->root)
        idunn_widget_free(win->root);
    win->root = root;
    win->hover = win->pressed = win->focus = NULL;
    if (root)
    {
        root->parent = NULL;
        idunn_set_window(root, win);
    }
    win->needs_layout = true;
}

void idunn_window_use_buffer(idunn_window_t *win, gfx_buffer_t *buf)
{
    if (win->own_buffer && win->buffer != buf)
        gfx_buffer_free(win->buffer);
    win->buffer = buf;
    win->needs_layout = true;
}

void idunn_window_resize(idunn_window_t *win, int32_t width, int32_t height)
{
    gfx_buffer_t *buf = gfx_buffer_new(width, height);
    if (!buf)
        return;
    idunn_window_use_buffer(win, buf);
    win->own_buffer = true;
}

gfx_buffer_t *idunn_window_buffer(const idunn_window_t *win)
{
    return win->buffer;
}

void idunn_window_invalidate(idunn_window_t *win)
{
    win->damage = gfx_rect(0, 0, win->buffer->width, win->buffer->height);
}

static bool touches(const idunn_widget_t *w, gfx_rect_t clip, int32_t overdraw)
{
    gfx_rect_t r = gfx_rect(w->rect.x - overdraw, w->rect.y - overdraw, w->rect.w + 2 * overdraw,
                            w->rect.h + 2 * overdraw);
    return !gfx_rect_empty(gfx_rect_intersect(r, clip));
}

static void draw_tree(idunn_widget_t *w, gfx_buffer_t *buf, gfx_rect_t clip, int32_t overdraw,
                      bool faded)
{
    if (!touches(w, clip, overdraw))
        return;

    // CSS opacity 0.5: draw the widget, then the old pixels over it at half
    gfx_buffer_t *under = NULL;
    gfx_rect_t area = {0};
    if ((w->state & IDUNN_DISABLED) && !faded)
    {
        area = gfx_rect_intersect(gfx_rect(w->rect.x - overdraw, w->rect.y - overdraw,
                                           w->rect.w + 2 * overdraw, w->rect.h + 2 * overdraw),
                                  clip);
        under = gfx_buffer_new(area.w, area.h);
        if (under)
            gfx_copy(under, 0, 0, buf, area);
        faded = true;
    }

    if (w->cls->draw)
        w->cls->draw(w, buf);
    for (int i = 0; i < w->child_count; i++)
        draw_tree(w->children[i], buf, clip, overdraw, faded);

    if (under)
    {
        gfx_blend(buf, area.x, area.y, under, gfx_rect(0, 0, area.w, area.h), 128);
        gfx_buffer_free(under);
    }
}

/**
 * place the widgets again if something changed their sizes. it does not
 * draw: the buffer can be on its way to the server.
 */
static void layout(idunn_window_t *win)
{
    if (!win->needs_layout || !win->buffer)
        return;
    win->needs_layout = false;
    if (win->root)
        idunn_place(win->root, gfx_rect(0, 0, win->buffer->width, win->buffer->height));
    idunn_window_invalidate(win);
}

gfx_rect_t idunn_window_draw(idunn_window_t *win)
{
    gfx_buffer_t *buf = win->buffer;
    if (!buf)
        return gfx_rect(0, 0, 0, 0);
    layout(win);
    gfx_rect_t clip = gfx_rect_intersect(win->damage, gfx_rect(0, 0, buf->width, buf->height));
    // drawing can ask for the next frame (a transition), so start anew
    win->damage = gfx_rect(0, 0, 0, 0);
    win->animating = false;
    if (gfx_rect_empty(clip))
        return clip;

    const idunn_theme_t *t = &win->ctx->theme;
    gfx_set_clip(buf, clip);
    gfx_fill_rect(buf, clip, idunn_color(t, IDUNN_COLOR_SURFACE));
    if (win->root)
        draw_tree(win->root, buf, clip, idunn_overdraw(t), false);
    gfx_reset_clip(buf);
    return clip;
}

bool idunn_window_tick(idunn_window_t *win, uint32_t now)
{
    win->now = now;
    for (size_t i = 0; i < sizeof(win->timers) / sizeof(win->timers[0]); i++)
    {
        idunn_widget_t *w = win->timers[i].widget;
        if (w && (int32_t)(now - win->timers[i].at) >= 0)
        {
            win->timers[i].widget = NULL;
            idunn_event_t ev = {.type = IDUNN_EV_TIMER};
            idunn_send(w, &ev);
        }
    }
    return win->animating;
}

uint32_t idunn_window_next_wake(const idunn_window_t *win)
{
    uint32_t next = 0;
    for (size_t i = 0; i < sizeof(win->timers) / sizeof(win->timers[0]); i++)
    {
        if (win->timers[i].widget && (!next || (int32_t)(win->timers[i].at - next) < 0))
            next = win->timers[i].at;
    }
    return next;
}

void idunn_wake(idunn_widget_t *w, uint32_t at)
{
    idunn_window_t *win = w->window;
    if (!win)
        return;
    size_t free_slot = sizeof(win->timers) / sizeof(win->timers[0]);
    for (size_t i = 0; i < sizeof(win->timers) / sizeof(win->timers[0]); i++)
    {
        if (win->timers[i].widget == w)
        {
            win->timers[i].at = at;
            return;
        }
        if (!win->timers[i].widget && i < free_slot)
            free_slot = i;
    }
    if (free_slot < sizeof(win->timers) / sizeof(win->timers[0]))
    {
        win->timers[free_slot].widget = w;
        win->timers[free_slot].at = at;
    }
}

void idunn_copy(idunn_widget_t *w, const char *text, size_t length)
{
    idunn_window_t *win = w->window;
    if (!win)
        return;
    if (win->clipboard_set)
    {
        win->clipboard_set(win, text, length);
        return;
    }
    char *copy = malloc(length + 1);
    if (!copy)
        return;
    memcpy(copy, text, length);
    copy[length] = '\0';
    free(win->clipboard);
    win->clipboard = copy;
    win->clipboard_length = length;
}

void idunn_paste(idunn_widget_t *w)
{
    idunn_window_t *win = w->window;
    if (!win)
        return;
    win->paste_to = w;
    if (win->clipboard_get)
        win->clipboard_get(win);
    else
        idunn_window_paste(win, win->clipboard ? win->clipboard : "", win->clipboard_length);
}

void idunn_window_paste(idunn_window_t *win, const char *text, size_t length)
{
    idunn_widget_t *w = win->paste_to;
    win->paste_to = NULL;
    if (!w || !text)
        return;
    idunn_event_t ev = {.type = IDUNN_EV_PASTE, .text = text, .length = length};
    idunn_send(w, &ev);
}

uint32_t idunn_window_cursor(const idunn_window_t *win)
{
    return win->cursor;
}

idunn_widget_t *idunn_window_focus(const idunn_window_t *win)
{
    return win->focus;
}

static uint32_t transition(const idunn_window_t *win)
{
    return win->ctx->theme.motion ? HOVER_MS : 0;
}

/**
 * the deepest interactive widget at (x, y), or NULL.
 */
static idunn_widget_t *hit(idunn_widget_t *w, int32_t x, int32_t y)
{
    if (!w || (w->state & IDUNN_DISABLED) || !gfx_rect_contains(w->rect, x, y))
        return NULL;
    for (int i = w->child_count - 1; i >= 0; i--)
    {
        idunn_widget_t *found = hit(w->children[i], x, y);
        if (found)
            return found;
    }
    return (w->flags & IDUNN_INTERACTIVE) ? w : NULL;
}

static void set_hover(idunn_window_t *win, idunn_widget_t *w)
{
    win->cursor = w ? w->cursor : IDUNN_CURSOR_ARROW;
    if (win->hover == w)
        return;
    if (win->hover)
    {
        win->hover->state &= ~IDUNN_HOVER;
        idunn_anim_to(&win->hover->hover, 0, win->now, transition(win));
        idunn_dirty(win->hover);
    }
    win->hover = w;
    if (w)
    {
        w->state |= IDUNN_HOVER;
        idunn_anim_to(&w->hover, 1, win->now, transition(win));
        idunn_dirty(w);
    }
}

static void set_focus(idunn_window_t *win, idunn_widget_t *w, bool visible)
{
    if (win->focus == w)
    {
        if (w && win->focus_visible != visible)
            idunn_dirty(w);
        win->focus_visible = visible;
        return;
    }
    idunn_widget_t *old = win->focus;
    win->focus = w;
    win->focus_visible = visible;
    if (old)
    {
        old->state &= ~IDUNN_FOCUSED;
        idunn_dirty(old);
        idunn_event_t ev = {.type = IDUNN_EV_BLUR};
        idunn_send(old, &ev);
    }
    if (w)
    {
        w->state |= IDUNN_FOCUSED;
        idunn_dirty(w);
        idunn_event_t ev = {.type = IDUNN_EV_FOCUS};
        idunn_send(w, &ev);
    }
}

static void set_pressed(idunn_window_t *win, idunn_widget_t *w, bool pressed)
{
    if (pressed)
        w->state |= IDUNN_PRESSED;
    else
        w->state &= ~IDUNN_PRESSED;
    idunn_anim_to(&w->press, pressed ? 1 : 0, win->now, transition(win));
    idunn_dirty(w);
}

bool idunn_send(idunn_widget_t *w, const idunn_event_t *ev)
{
    return w->cls->event && w->cls->event(w, ev);
}

/**
 * how many clicks a press makes: 2 or 3 when it comes soon after the last
 * one, near it.
 */
static int count_clicks(idunn_window_t *win, int32_t x, int32_t y)
{
    bool near = abs(x - win->press_x) <= DOUBLE_CLICK_PX && abs(y - win->press_y) <= DOUBLE_CLICK_PX;
    bool soon = win->clicks && win->now - win->press_time <= DOUBLE_CLICK_MS;
    win->clicks = near && soon ? (win->clicks % 3) + 1 : 1;
    win->press_time = win->now;
    win->press_x = x;
    win->press_y = y;
    return win->clicks;
}

void idunn_window_pointer(idunn_window_t *win, idunn_pointer_t kind, int32_t x, int32_t y,
                          uint32_t mods)
{
    layout(win); // the rects must be where they are drawn
    idunn_widget_t *under = kind == IDUNN_POINTER_LEAVE ? NULL : hit(win->root, x, y);
    switch (kind)
    {
    case IDUNN_POINTER_MOTION:
    case IDUNN_POINTER_LEAVE:
        set_hover(win, under);
        if (win->pressed && kind == IDUNN_POINTER_MOTION)
        {
            idunn_event_t ev = {.type = IDUNN_EV_DRAG, .x = x, .y = y, .mods = mods};
            idunn_send(win->pressed, &ev);
        }
        break;
    case IDUNN_POINTER_PRESS:
    {
        int clicks = count_clicks(win, x, y);
        set_hover(win, under);
        win->pressed = under;
        if (under)
            set_pressed(win, under, true);
        // a click takes the focus without the ring, as :focus-visible
        set_focus(win, under && (under->flags & IDUNN_FOCUSABLE) ? under : NULL, false);
        if (under)
        {
            idunn_event_t ev = {
                .type = IDUNN_EV_PRESS, .x = x, .y = y, .mods = mods, .clicks = clicks};
            idunn_send(under, &ev);
        }
        break;
    }
    case IDUNN_POINTER_RELEASE:
    {
        idunn_widget_t *pressed = win->pressed;
        win->pressed = NULL;
        if (!pressed)
            break;
        set_pressed(win, pressed, false);
        idunn_event_t release = {.type = IDUNN_EV_RELEASE, .x = x, .y = y, .mods = mods};
        idunn_send(pressed, &release);
        if (pressed == under)
        {
            idunn_event_t ev = {.type = IDUNN_EV_CLICK};
            idunn_send(pressed, &ev);
        }
        break;
    }
    }
}

/**
 * the focusable widgets in tree order: the one after (or before) from.
 */
static void collect(idunn_widget_t *w, idunn_widget_t **list, int *count, int max)
{
    if (!w || (w->state & IDUNN_DISABLED))
        return;
    if ((w->flags & IDUNN_FOCUSABLE) && *count < max)
        list[(*count)++] = w;
    for (int i = 0; i < w->child_count; i++)
        collect(w->children[i], list, count, max);
}

static void move_focus(idunn_window_t *win, bool back)
{
    enum { MAX = 512 };
    idunn_widget_t *list[MAX];
    int count = 0;
    collect(win->root, list, &count, MAX);
    if (count == 0)
        return;
    int at = -1;
    for (int i = 0; i < count; i++)
    {
        if (list[i] == win->focus)
            at = i;
    }
    int next = at < 0 ? (back ? count - 1 : 0) : (at + (back ? count - 1 : 1)) % count;
    set_focus(win, list[next], true);
}

void idunn_window_key(idunn_window_t *win, uint16_t code, uint32_t mods, const char *text)
{
    if (code == IDUNN_KEY_TAB && !(mods & (IDUNN_MOD_CTRL | IDUNN_MOD_ALT)))
    {
        move_focus(win, mods & IDUNN_MOD_SHIFT);
        return;
    }
    idunn_event_t ev = {.type = IDUNN_EV_KEY, .code = code, .mods = mods, .text = text ? text : ""};
    for (idunn_widget_t *w = win->focus; w; w = w->parent)
    {
        if (idunn_send(w, &ev))
            return;
    }
}

void idunn_window_scroll(idunn_window_t *win, int32_t x, int32_t y, int32_t steps)
{
    // the widget under the pointer, or the first of its parents that scrolls
    idunn_widget_t *w = win->root;
    while (w)
    {
        idunn_widget_t *inner = NULL;
        for (int i = w->child_count - 1; i >= 0 && !inner; i--)
        {
            if (gfx_rect_contains(w->children[i]->rect, x, y))
                inner = w->children[i];
        }
        if (!inner)
            break;
        w = inner;
    }
    idunn_event_t ev = {.type = IDUNN_EV_SCROLL, .x = x, .y = y, .steps = steps};
    for (; w; w = w->parent)
    {
        if (!(w->state & IDUNN_DISABLED) && idunn_send(w, &ev))
            return;
    }
}
