/**
 * @file widget.c
 * @brief idunn: the widget tree, sizes, what must be drawn again, and
 *        transitions.
 */

#include "idunn_internal.h"

#include <stdlib.h>

idunn_widget_t *idunn_widget_new(const idunn_class_t *cls, void *data, uint32_t flags)
{
    idunn_widget_t *w = calloc(1, sizeof(*w));
    if (!w)
        return NULL;
    w->cls = cls;
    w->data = data;
    w->flags = flags;
    return w;
}

idunn_widget_t *idunn_add(idunn_widget_t *parent, idunn_widget_t *child)
{
    if (!parent || !child)
        return child;
    if (parent->child_count == parent->child_capacity)
    {
        int capacity = parent->child_capacity ? parent->child_capacity * 2 : 4;
        idunn_widget_t **children =
            realloc(parent->children, (size_t)capacity * sizeof(*children));
        if (!children)
            return child;
        parent->children = children;
        parent->child_capacity = capacity;
    }
    parent->children[parent->child_count++] = child;
    child->parent = parent;
    idunn_set_window(child, parent->window);
    idunn_relayout(parent);
    return child;
}

/**
 * take w out of the window's hover, press and focus.
 */
static void forget(idunn_widget_t *w)
{
    idunn_window_t *win = w->window;
    if (!win)
        return;
    if (win->hover == w)
        win->hover = NULL;
    if (win->pressed == w)
        win->pressed = NULL;
    if (win->focus == w)
        win->focus = NULL;
    if (win->paste_to == w)
        win->paste_to = NULL;
    for (size_t i = 0; i < sizeof(win->timers) / sizeof(win->timers[0]); i++)
    {
        if (win->timers[i].widget == w)
            win->timers[i].widget = NULL;
    }
}

static void free_tree(idunn_widget_t *w)
{
    for (int i = 0; i < w->child_count; i++)
        free_tree(w->children[i]);
    forget(w);
    if (w->cls->free)
        w->cls->free(w);
    free(w->children);
    free(w);
}

void idunn_widget_free(idunn_widget_t *w)
{
    if (!w)
        return;
    idunn_widget_t *parent = w->parent;
    if (parent)
    {
        for (int i = 0; i < parent->child_count; i++)
        {
            if (parent->children[i] == w)
            {
                for (int j = i + 1; j < parent->child_count; j++)
                    parent->children[j - 1] = parent->children[j];
                parent->child_count--;
                break;
            }
        }
        idunn_relayout(parent);
    }
    else if (w->window && w->window->root == w)
    {
        w->window->root = NULL;
    }
    free_tree(w);
}

void idunn_set_window(idunn_widget_t *w, idunn_window_t *win)
{
    w->window = win;
    for (int i = 0; i < w->child_count; i++)
        idunn_set_window(w->children[i], win);
}

idunn_widget_t *idunn_grow(idunn_widget_t *w, int weight)
{
    if (w)
    {
        w->grow = weight > 0 ? weight : 0;
        idunn_relayout(w);
    }
    return w;
}

void idunn_set_size(idunn_widget_t *w, int32_t width, int32_t height)
{
    w->width = width > 0 ? width : 0;
    w->height = height > 0 ? height : 0;
    idunn_relayout(w);
}

void idunn_set_disabled(idunn_widget_t *w, bool disabled)
{
    if (disabled == ((w->state & IDUNN_DISABLED) != 0))
        return;
    if (disabled)
    {
        w->state = (w->state | IDUNN_DISABLED) & ~(IDUNN_HOVER | IDUNN_PRESSED);
        w->hover = (idunn_anim_t){0};
        w->press = (idunn_anim_t){0};
        forget(w);
        w->state &= ~IDUNN_FOCUSED;
    }
    else
    {
        w->state &= ~IDUNN_DISABLED;
    }
    idunn_dirty(w);
}

void idunn_set_error(idunn_widget_t *w, bool error)
{
    if (error)
        w->state |= IDUNN_ERROR;
    else
        w->state &= ~IDUNN_ERROR;
    idunn_dirty(w);
}

gfx_rect_t idunn_widget_rect(const idunn_widget_t *w)
{
    return w->rect;
}

void idunn_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    int32_t mw = 0;
    int32_t mh = 0;
    const idunn_theme_t *t = idunn_theme(w);
    if (w->width)
        max_width = idunn_px(t, w->width);
    if (w->cls->measure)
        w->cls->measure(w, max_width, &mw, &mh);
    *width = w->width ? idunn_px(t, w->width) : mw;
    *height = w->height ? idunn_px(t, w->height) : mh;
}

void idunn_place(idunn_widget_t *w, gfx_rect_t rect)
{
    w->rect = rect;
    if (w->cls->layout)
        w->cls->layout(w);
}

void idunn_dirty(idunn_widget_t *w)
{
    idunn_window_t *win = w->window;
    if (!win || gfx_rect_empty(w->rect))
        return;
    int32_t m = idunn_overdraw(&win->ctx->theme);
    gfx_rect_t r = gfx_rect(w->rect.x - m, w->rect.y - m, w->rect.w + 2 * m, w->rect.h + 2 * m);
    win->damage = gfx_rect_empty(win->damage) ? r : gfx_rect_union(win->damage, r);
}

void idunn_relayout(idunn_widget_t *w)
{
    if (w->window)
        w->window->needs_layout = true;
}

bool idunn_ring_visible(const idunn_widget_t *w)
{
    // a text input shows it after a click too (:focus-within in the preview)
    return (w->state & IDUNN_FOCUSED) && w->window &&
           (w->window->focus_visible || (w->flags & IDUNN_TEXT_INPUT));
}

static uint8_t amount(idunn_widget_t *w, const idunn_anim_t *a)
{
    uint32_t now = w->window ? w->window->now : 0;
    if (w->window && idunn_anim_running(a, now))
    {
        // draw it again in the next frame
        w->window->animating = true;
        idunn_dirty(w);
    }
    return (uint8_t)(idunn_anim_value(a, now) * 255 + 0.5f);
}

uint8_t idunn_hover_amount(idunn_widget_t *w)
{
    return amount(w, &w->hover);
}

uint8_t idunn_press_amount(idunn_widget_t *w)
{
    return amount(w, &w->press);
}

void idunn_anim_to(idunn_anim_t *a, float to, uint32_t now, uint32_t duration)
{
    a->from = idunn_anim_value(a, now);
    a->to = to;
    a->start = now;
    a->duration = duration;
}

float idunn_anim_value(const idunn_anim_t *a, uint32_t now)
{
    if (a->duration == 0 || now - a->start >= a->duration)
        return a->to;
    // ease-out: fast at first, slow at the end
    float t = (float)(now - a->start) / (float)a->duration;
    float e = 1.0f - (1.0f - t) * (1.0f - t);
    return a->from + (a->to - a->from) * e;
}

bool idunn_anim_running(const idunn_anim_t *a, uint32_t now)
{
    return a->duration != 0 && now - a->start < a->duration && a->from != a->to;
}
