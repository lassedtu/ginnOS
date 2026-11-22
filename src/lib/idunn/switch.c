/**
 * @file switch.c
 * @brief idunn: a switch, 32 by 18 pixels: a round track (the input
 *        colour when off, the accent when on) and a white 14 pixel thumb
 *        that slides in 120 ms.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define WIDTH    32
#define HEIGHT   18
#define THUMB    14
#define INSET    2
#define SLIDE_MS 120

typedef struct
{
    char *label;
    bool on;
    idunn_anim_t slide; // 0 off, 1 on
    idunn_callback_t on_change;
    void *data;
} switch_t;

static void switch_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)max_width;
    const switch_t *s = w->data;
    idunn_option_measure(w, WIDTH, HEIGHT, s->label, width, height);
}

static void switch_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    switch_t *s = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    gfx_rect_t track = idunn_option_control(w, WIDTH, HEIGHT);
    int32_t round = track.h / 2;

    float at = idunn_anim_value(&s->slide, w->window->now);
    if (idunn_anim_running(&s->slide, w->window->now))
    {
        w->window->animating = true;
        idunn_dirty(w);
    }

    gfx_color_t off = t->mode == IDUNN_DARK ? gfx_rgba(255, 255, 255, 46)
                                            : idunn_color(t, IDUNN_COLOR_INPUT);
    if (idunn_ring_visible(w))
        idunn_draw_ring(w, buf, track, round);
    gfx_fill_round_rect(buf, track, round,
                        gfx_mix(off, idunn_color(t, IDUNN_COLOR_ACCENT), (uint8_t)(at * 255)));

    int32_t size = idunn_px(t, THUMB);
    int32_t inset = idunn_px(t, INSET);
    int32_t travel = track.w - 2 * inset - size;
    gfx_rect_t thumb = gfx_rect(track.x + inset + (int32_t)(travel * at + 0.5f),
                                track.y + (track.h - size) / 2, size, size);
    gfx_shadow_t drop = {0, idunn_px(t, 1), idunn_px(t, 2), 0, gfx_rgba(0, 0, 0, 64), false};
    gfx_box_shadow(buf, thumb, size / 2, &drop);
    gfx_fill_round_rect(buf, thumb, size / 2,
                        s->on && t->mode == IDUNN_DARK
                            ? idunn_color(t, IDUNN_COLOR_ACCENT_FOREGROUND)
                            : gfx_rgb(255, 255, 255));
    idunn_option_draw_label(w, buf, WIDTH, s->label);
}

static void set_on(idunn_widget_t *w, bool on, bool tell)
{
    switch_t *s = w->data;
    if (s->on == on)
        return;
    s->on = on;
    uint32_t now = w->window ? w->window->now : 0;
    bool motion = !w->window || w->window->ctx->theme.motion;
    idunn_anim_to(&s->slide, on ? 1 : 0, now, motion && w->window ? SLIDE_MS : 0);
    idunn_dirty(w);
    if (tell && s->on_change)
        s->on_change(w, s->data);
}

static bool switch_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    switch_t *s = w->data;
    if (ev->type == IDUNN_EV_CLICK || (ev->type == IDUNN_EV_KEY && ev->code == IDUNN_KEY_SPACE))
    {
        set_on(w, !s->on, true);
        return true;
    }
    return false;
}

static void switch_free(idunn_widget_t *w)
{
    switch_t *s = w->data;
    free(s->label);
    free(s);
}

static const idunn_class_t switch_class = {
    .name = "switch",
    .measure = switch_measure,
    .draw = switch_draw,
    .event = switch_event,
    .free = switch_free,
};

idunn_widget_t *idunn_switch(const char *label, bool on, idunn_callback_t on_change, void *data)
{
    switch_t *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->label = label ? strdup(label) : NULL;
    s->on = on;
    s->slide = (idunn_anim_t){on ? 1.0f : 0.0f, on ? 1.0f : 0.0f, 0, 0};
    s->on_change = on_change;
    s->data = data;
    idunn_widget_t *w = idunn_widget_new(&switch_class, s, IDUNN_FOCUSABLE | IDUNN_INTERACTIVE);
    if (!w)
    {
        free(s->label);
        free(s);
    }
    return w;
}

static switch_t *as_switch(const idunn_widget_t *w)
{
    return w && w->cls == &switch_class ? w->data : NULL;
}

bool idunn_switch_on(const idunn_widget_t *sw)
{
    const switch_t *s = as_switch(sw);
    return s && s->on;
}

void idunn_switch_set_on(idunn_widget_t *sw, bool on)
{
    if (as_switch(sw))
        set_on(sw, on, false);
}
