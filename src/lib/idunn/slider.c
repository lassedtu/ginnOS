/**
 * @file slider.c
 * @brief idunn: a slider: a 4 pixel round track in the muted colour,
 *        filled with the accent up to a 16 pixel thumb. a press puts the
 *        thumb under the pointer and dragging moves it; the arrows move it
 *        by a step, Page Up and Page Down by ten, Home and End to the ends.
 */

#include "idunn_internal.h"

#include <stdlib.h>

#define HEIGHT 16
#define TRACK  4
#define THUMB  16
#define WIDTH  200 // when nothing else gives it a width

typedef struct
{
    int32_t min;
    int32_t max;
    int32_t step;
    int32_t value;
    idunn_callback_t on_change;
    void *data;
} slider_t;

static void slider_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)max_width;
    const idunn_theme_t *t = idunn_theme(w);
    *width = idunn_px(t, WIDTH);
    *height = idunn_px(t, HEIGHT);
}

/** the x of the thumb's middle for a value */
static int32_t x_of(const idunn_widget_t *w, int32_t value)
{
    const slider_t *s = w->data;
    int32_t half = idunn_px(idunn_theme(w), THUMB) / 2;
    int32_t span = s->max > s->min ? s->max - s->min : 1;
    return w->rect.x + half + (int32_t)((int64_t)(w->rect.w - 2 * half) * (value - s->min) / span);
}

static gfx_rect_t thumb_rect(const idunn_widget_t *w)
{
    const slider_t *s = w->data;
    int32_t size = idunn_px(idunn_theme(w), THUMB);
    return gfx_rect(x_of(w, s->value) - size / 2, w->rect.y + (w->rect.h - size) / 2, size, size);
}

static void slider_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const idunn_theme_t *t = idunn_theme(w);
    int32_t h = idunn_px(t, TRACK);
    gfx_rect_t track = gfx_rect(w->rect.x, w->rect.y + (w->rect.h - h) / 2, w->rect.w, h);
    gfx_rect_t thumb = thumb_rect(w);
    gfx_fill_round_rect(buf, track, h / 2, idunn_color(t, IDUNN_COLOR_MUTED));
    gfx_rect_t filled = track;
    filled.w = thumb.x + thumb.w / 2 - track.x;
    gfx_fill_round_rect(buf, filled, h / 2, idunn_color(t, IDUNN_COLOR_ACCENT));

    if (idunn_ring_visible(w))
        idunn_draw_ring(w, buf, thumb, thumb.w / 2);
    idunn_draw_shadow(t, buf, thumb, thumb.w / 2, IDUNN_SHADOW_XS);
    gfx_fill_round_rect(buf, thumb, thumb.w / 2, idunn_color(t, IDUNN_COLOR_BACKGROUND));
    gfx_border_round_rect(buf, thumb, thumb.w / 2, idunn_px(t, 1),
                          idunn_color(t, IDUNN_COLOR_ACCENT));
}

static void set_value(idunn_widget_t *w, int32_t value, bool tell)
{
    slider_t *s = w->data;
    if (s->step > 1)
        value = s->min + (value - s->min + s->step / 2) / s->step * s->step;
    value = value < s->min ? s->min : value > s->max ? s->max : value;
    if (value == s->value)
        return;
    s->value = value;
    idunn_dirty(w);
    if (tell && s->on_change)
        s->on_change(w, s->data);
}

/** the value under a window x */
static int32_t value_at(const idunn_widget_t *w, int32_t x)
{
    const slider_t *s = w->data;
    int32_t half = idunn_px(idunn_theme(w), THUMB) / 2;
    int32_t room = w->rect.w - 2 * half;
    if (room <= 0)
        return s->min;
    int64_t offset = x - (w->rect.x + half);
    int64_t span = s->max - s->min;
    return s->min + (int32_t)((offset * span + (offset >= 0 ? room / 2 : -room / 2)) / room);
}

static bool slider_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    slider_t *s = w->data;
    switch (ev->type)
    {
    case IDUNN_EV_PRESS:
    case IDUNN_EV_DRAG:
        set_value(w, value_at(w, ev->x), true);
        return true;
    case IDUNN_EV_KEY:
        switch (ev->code)
        {
        case IDUNN_KEY_LEFT:
        case IDUNN_KEY_DOWN:
            set_value(w, s->value - s->step, true);
            return true;
        case IDUNN_KEY_RIGHT:
        case IDUNN_KEY_UP:
            set_value(w, s->value + s->step, true);
            return true;
        case IDUNN_KEY_PAGEDOWN:
            set_value(w, s->value - 10 * s->step, true);
            return true;
        case IDUNN_KEY_PAGEUP:
            set_value(w, s->value + 10 * s->step, true);
            return true;
        case IDUNN_KEY_HOME:
            set_value(w, s->min, true);
            return true;
        case IDUNN_KEY_END:
            set_value(w, s->max, true);
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}

static void slider_free(idunn_widget_t *w)
{
    free(w->data);
}

static const idunn_class_t slider_class = {
    .name = "slider",
    .measure = slider_measure,
    .draw = slider_draw,
    .event = slider_event,
    .free = slider_free,
};

idunn_widget_t *idunn_slider(int32_t min, int32_t max, int32_t step, int32_t value,
                             idunn_callback_t on_change, void *data)
{
    slider_t *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->min = min;
    s->max = max > min ? max : min;
    s->step = step > 0 ? step : 1;
    s->value = value < s->min ? s->min : value > s->max ? s->max : value;
    s->on_change = on_change;
    s->data = data;
    idunn_widget_t *w = idunn_widget_new(&slider_class, s, IDUNN_FOCUSABLE | IDUNN_INTERACTIVE);
    if (!w)
        free(s);
    return w;
}

static slider_t *as_slider(const idunn_widget_t *w)
{
    return w && w->cls == &slider_class ? w->data : NULL;
}

int32_t idunn_slider_value(const idunn_widget_t *slider)
{
    const slider_t *s = as_slider(slider);
    return s ? s->value : 0;
}

void idunn_slider_set_value(idunn_widget_t *slider, int32_t value)
{
    if (as_slider(slider))
        set_value(slider, value, false);
}
