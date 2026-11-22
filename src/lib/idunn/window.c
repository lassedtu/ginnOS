/**
 * @file window.c
 * @brief idunn: a window's widgets: layout, drawing what changed, and
 *        the pointer and the keys. no window server here (app.c has it),
 *        so the host tests can drive a window.
 */

#include "idunn_internal.h"

#include <stdlib.h>

#define HOVER_MS 100

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

gfx_rect_t idunn_window_draw(idunn_window_t *win)
{
    gfx_buffer_t *buf = win->buffer;
    if (!buf)
        return gfx_rect(0, 0, 0, 0);
    if (win->needs_layout)
    {
        win->needs_layout = false;
        if (win->root)
            idunn_place(win->root, gfx_rect(0, 0, buf->width, buf->height));
        idunn_window_invalidate(win);
    }
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
    return win->animating;
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
    if (win->focus == w && win->focus_visible == visible)
        return;
    if (win->focus)
    {
        win->focus->state &= ~IDUNN_FOCUSED;
        idunn_dirty(win->focus);
    }
    win->focus = w;
    win->focus_visible = visible;
    if (w)
    {
        w->state |= IDUNN_FOCUSED;
        idunn_dirty(w);
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

static bool send(idunn_widget_t *w, const idunn_event_t *ev)
{
    return w->cls->event && w->cls->event(w, ev);
}

void idunn_window_pointer(idunn_window_t *win, idunn_pointer_t kind, int32_t x, int32_t y)
{
    if (win->needs_layout)
        idunn_window_draw(win); // the rects must be where they are drawn
    idunn_widget_t *under = kind == IDUNN_POINTER_LEAVE ? NULL : hit(win->root, x, y);
    switch (kind)
    {
    case IDUNN_POINTER_MOTION:
    case IDUNN_POINTER_LEAVE:
        set_hover(win, under);
        break;
    case IDUNN_POINTER_PRESS:
        set_hover(win, under);
        win->pressed = under;
        if (under)
            set_pressed(win, under, true);
        // a click takes the focus without the ring, as :focus-visible
        set_focus(win, under && (under->flags & IDUNN_FOCUSABLE) ? under : NULL, false);
        break;
    case IDUNN_POINTER_RELEASE:
    {
        idunn_widget_t *pressed = win->pressed;
        win->pressed = NULL;
        if (!pressed)
            break;
        set_pressed(win, pressed, false);
        if (pressed == under)
        {
            idunn_event_t ev = {.type = IDUNN_EV_CLICK};
            send(pressed, &ev);
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
        if (send(w, &ev))
            return;
    }
}
