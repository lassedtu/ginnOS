/**
 * @file scroll.c
 * @brief idunn: a scroll view: one child that can be taller than the
 *        view. the wheel, the keys a child does not use (the arrows, Page
 *        Up and Page Down, Home and End) and a thin scroll bar move it. the
 *        bar is 6 pixels wide, 10 near the pointer, and shows only when
 *        there is something to scroll. wheel and key steps glide in 150 ms.
 */

#include "idunn_internal.h"

#include <stdlib.h>

#define STEP      48  // pixels a wheel step or an arrow scrolls
#define BAR       6
#define BAR_NEAR  10  // the bar under the pointer
#define NEAR      14  // how near to the right edge the pointer makes the bar wide
#define INSET     2
#define THUMB_MIN 24
#define GLIDE_MS  150
#define MARGIN    8   // space kept around a revealed widget

typedef struct
{
    int32_t content;  // the height of the child
    int32_t placed;   // the scroll the child is placed at
    idunn_anim_t to;  // the scroll it glides to (pixels)
    bool near;        // the pointer is near the bar
    bool dragging;
    int32_t grab;     // where in the thumb the drag started
} scroll_t;

static int32_t most(const idunn_widget_t *w)
{
    const scroll_t *s = w->data;
    int32_t m = s->content - w->rect.h;
    return m > 0 ? m : 0;
}

static int32_t target(const idunn_widget_t *w)
{
    const scroll_t *s = w->data;
    return (int32_t)s->to.to;
}

static void place_child(idunn_widget_t *w, int32_t offset)
{
    scroll_t *s = w->data;
    s->placed = offset;
    if (w->child_count)
        idunn_place(w->children[0], gfx_rect(w->rect.x, w->rect.y - offset, w->rect.w, s->content));
}

/**
 * the strip the bar is drawn in, at its widest.
 */
static gfx_rect_t bar_strip(idunn_widget_t *w)
{
    int32_t width = idunn_px(idunn_theme(w), BAR_NEAR + 2 * INSET);
    return gfx_rect(w->rect.x + w->rect.w - width, w->rect.y, width, w->rect.h);
}

/**
 * show the content from offset: the pixels that stay in view move, and
 * only the strip that comes into view (and the bar) is drawn again.
 */
static void move_to(idunn_widget_t *w, int32_t offset)
{
    scroll_t *s = w->data;
    int32_t dy = s->placed - offset;
    place_child(w, offset);
    idunn_window_move_pixels(w, w->rect, dy);
    idunn_dirty_rect(w, bar_strip(w));
}

/**
 * scroll to offset (kept in range), gliding or at once.
 * @return false if it was there already.
 */
static bool scroll_to(idunn_widget_t *w, int32_t offset, bool glide)
{
    scroll_t *s = w->data;
    int32_t m = most(w);
    offset = offset < 0 ? 0 : offset > m ? m : offset;
    if (offset == target(w))
        return false;
    bool motion = w->window && w->window->ctx->theme.motion;
    uint32_t now = w->window ? w->window->now : 0;
    idunn_anim_to(&s->to, (float)offset, now, glide && motion ? GLIDE_MS : 0);
    if (!glide || !motion)
        move_to(w, offset);
    else
        idunn_dirty_rect(w, bar_strip(w)); // a frame, where the glide starts
    return true;
}

static void scroll_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    // as CSS min-height: 0, it takes the height it is given (grow, or a size)
    *width = 0;
    *height = 0;
    if (w->child_count)
    {
        int32_t h;
        idunn_measure(w->children[0], max_width, width, &h);
    }
}

static void scroll_layout(idunn_widget_t *w)
{
    scroll_t *s = w->data;
    int32_t width;
    int32_t height = 0;
    if (w->child_count)
        idunn_measure(w->children[0], w->rect.w, &width, &height);
    s->content = height > w->rect.h ? height : w->rect.h;
    int32_t m = most(w);
    if (target(w) > m)
        s->to = (idunn_anim_t){(float)m, (float)m, 0, 0};
    int32_t now = (int32_t)(idunn_anim_value(&s->to, w->window ? w->window->now : 0) + 0.5f);
    place_child(w, now > m ? m : now);
}

static void scroll_frame(idunn_widget_t *w)
{
    scroll_t *s = w->data;
    uint32_t now = w->window->now;
    int32_t at = (int32_t)(idunn_anim_value(&s->to, now) + 0.5f);
    if (at != s->placed)
        move_to(w, at);
    if (idunn_anim_running(&s->to, now))
    {
        w->window->animating = true;
        idunn_dirty_rect(w, bar_strip(w)); // the next frame
    }
}

/** the thumb of the bar, or an empty rect when nothing scrolls */
static gfx_rect_t thumb_rect(const idunn_widget_t *w, int32_t width)
{
    const scroll_t *s = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    int32_t m = most(w);
    if (m <= 0)
        return gfx_rect(0, 0, 0, 0);
    int32_t inset = idunn_px(t, INSET);
    int32_t track = w->rect.h - 2 * inset;
    int32_t h = (int32_t)((int64_t)track * w->rect.h / s->content);
    int32_t least = idunn_px(t, THUMB_MIN);
    h = h < least ? least : h;
    int32_t y = w->rect.y + inset + (int32_t)((int64_t)(track - h) * s->placed / m);
    return gfx_rect(w->rect.x + w->rect.w - inset - width, y, width, h);
}

static void scroll_draw_over(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const scroll_t *s = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    bool wide = s->dragging || (s->near && (w->state & IDUNN_HOVER));
    gfx_rect_t thumb = thumb_rect(w, idunn_px(t, wide ? BAR_NEAR : BAR));
    if (gfx_rect_empty(thumb))
        return;
    gfx_color_t color = idunn_color(t, IDUNN_COLOR_BORDER);
    if (wide)
        color = gfx_mix(color, idunn_color(t, IDUNN_COLOR_MUTED_FOREGROUND), 96);
    gfx_fill_round_rect(buf, thumb, thumb.w / 2, color);
}

static bool near_bar(const idunn_widget_t *w, int32_t x)
{
    return most(w) > 0 && x >= w->rect.x + w->rect.w - idunn_px(idunn_theme(w), NEAR);
}

static bool scroll_key(idunn_widget_t *w, const idunn_event_t *ev)
{
    if (ev->mods & (IDUNN_MOD_CTRL | IDUNN_MOD_ALT))
        return false;
    int32_t step = idunn_px(idunn_theme(w), STEP);
    int32_t page = w->rect.h - step > step ? w->rect.h - step : step;
    switch (ev->code)
    {
    case IDUNN_KEY_UP:
        return scroll_to(w, target(w) - step, true);
    case IDUNN_KEY_DOWN:
        return scroll_to(w, target(w) + step, true);
    case IDUNN_KEY_PAGEUP:
        return scroll_to(w, target(w) - page, true);
    case IDUNN_KEY_PAGEDOWN:
        return scroll_to(w, target(w) + page, true);
    case IDUNN_KEY_HOME:
        return scroll_to(w, 0, true);
    case IDUNN_KEY_END:
        return scroll_to(w, most(w), true);
    default:
        return false;
    }
}

static bool scroll_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    scroll_t *s = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    switch (ev->type)
    {
    case IDUNN_EV_SCROLL:
        // at an end the parents can scroll
        return scroll_to(w, target(w) - ev->steps * idunn_px(t, STEP), true);
    case IDUNN_EV_KEY:
        return scroll_key(w, ev);
    case IDUNN_EV_MOTION:
        if (near_bar(w, ev->x) != s->near)
        {
            s->near = !s->near;
            idunn_dirty_rect(w, bar_strip(w));
        }
        return true;
    case IDUNN_EV_PRESS:
    {
        if (!near_bar(w, ev->x))
            return false;
        gfx_rect_t thumb = thumb_rect(w, idunn_px(t, BAR_NEAR));
        if (ev->y >= thumb.y && ev->y < thumb.y + thumb.h)
        {
            s->dragging = true;
            s->grab = ev->y - thumb.y;
            idunn_dirty_rect(w, bar_strip(w));
        }
        else
        {
            // a press on the track moves a page that way
            int32_t page = w->rect.h - idunn_px(t, STEP);
            scroll_to(w, target(w) + (ev->y < thumb.y ? -page : page), true);
        }
        return true;
    }
    case IDUNN_EV_DRAG:
    {
        if (!s->dragging)
            return false;
        gfx_rect_t thumb = thumb_rect(w, idunn_px(t, BAR_NEAR));
        int32_t inset = idunn_px(t, INSET);
        int32_t room = w->rect.h - 2 * inset - thumb.h;
        if (room > 0)
            scroll_to(w, (int32_t)((int64_t)(ev->y - s->grab - w->rect.y - inset) * most(w) / room),
                      false);
        return true;
    }
    case IDUNN_EV_RELEASE:
        if (s->dragging)
        {
            s->dragging = false;
            idunn_dirty_rect(w, bar_strip(w));
        }
        return true;
    default:
        return false;
    }
}

static gfx_rect_t scroll_reveal(idunn_widget_t *w, gfx_rect_t r)
{
    const scroll_t *s = w->data;
    int32_t margin = idunn_px(idunn_theme(w), MARGIN);
    // r in the content, from where the child is placed now
    int32_t top = r.y - (w->rect.y - s->placed);
    int32_t want = target(w);
    if (top + r.h + margin > want + w->rect.h)
        want = top + r.h + margin - w->rect.h;
    if (top - margin < want)
        want = top - margin;
    scroll_to(w, want, true);
    // the views around this one need to show only what this one shows
    r.y += s->placed - target(w);
    return gfx_rect_intersect(r, w->rect);
}

static void scroll_free(idunn_widget_t *w)
{
    free(w->data);
}

static const idunn_class_t scroll_class = {
    .name = "scroll view",
    .measure = scroll_measure,
    .layout = scroll_layout,
    .frame = scroll_frame,
    .draw_over = scroll_draw_over,
    .event = scroll_event,
    .reveal = scroll_reveal,
    .hover_area = bar_strip,
    .free = scroll_free,
};

idunn_widget_t *idunn_scroll_view(idunn_widget_t *child)
{
    scroll_t *s = calloc(1, sizeof(*s));
    idunn_widget_t *w = s ? idunn_widget_new(&scroll_class, s, IDUNN_INTERACTIVE | IDUNN_CLIPS)
                          : NULL;
    if (!w)
    {
        free(s);
        idunn_widget_free(child);
        return NULL;
    }
    if (child)
        idunn_add(w, child);
    return w;
}

int32_t idunn_scroll_view_offset(const idunn_widget_t *view)
{
    return view && view->cls == &scroll_class ? target(view) : 0;
}

void idunn_scroll_view_scroll_to(idunn_widget_t *view, int32_t offset)
{
    if (view && view->cls == &scroll_class)
        scroll_to(view, offset, false);
}
