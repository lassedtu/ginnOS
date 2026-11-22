/**
 * @file check.c
 * @brief idunn: a check box, 16 pixels, with a label. checked: an accent
 *        fill and a white check; mixed: a dash.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define SIZE 16
#define MARK 12

typedef struct
{
    char *label;
    idunn_check_t state;
    idunn_callback_t on_change;
    void *data;
} check_t;

static void check_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)max_width;
    const check_t *c = w->data;
    idunn_option_measure(w, SIZE, SIZE, c->label, width, height);
}

static void check_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const check_t *c = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    gfx_rect_t box = idunn_option_control(w, SIZE, SIZE);
    int32_t radius = idunn_radius_sm(t);
    bool on = c->state != IDUNN_UNCHECKED;

    if (idunn_ring_visible(w))
        idunn_draw_ring(w, buf, box, radius);
    idunn_draw_shadow(t, buf, box, radius, IDUNN_SHADOW_XS);
    if (on)
        gfx_fill_round_rect(buf, box, radius, idunn_color(t, IDUNN_COLOR_ACCENT));
    gfx_border_round_rect(buf, box, radius, idunn_px(t, 1),
                          on ? idunn_color(t, IDUNN_COLOR_ACCENT) : idunn_option_border(t));
    if (on)
    {
        const icon_t *mark = idunn_icon(ctx, c->state == IDUNN_MIXED ? "minus" : "check", MARK);
        int32_t size = idunn_px(t, MARK);
        if (mark)
            icon_draw_mask(buf, mark, box.x + (box.w - size) / 2, box.y + (box.h - size) / 2,
                           idunn_color(t, IDUNN_COLOR_ACCENT_FOREGROUND));
    }
    idunn_option_draw_label(w, buf, SIZE, c->label);
}

static void toggle(idunn_widget_t *w)
{
    check_t *c = w->data;
    c->state = c->state == IDUNN_CHECKED ? IDUNN_UNCHECKED : IDUNN_CHECKED;
    idunn_dirty(w);
    if (c->on_change)
        c->on_change(w, c->data);
}

static bool check_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    if (ev->type == IDUNN_EV_CLICK || (ev->type == IDUNN_EV_KEY && ev->code == IDUNN_KEY_SPACE))
    {
        toggle(w);
        return true;
    }
    return false;
}

static void check_free(idunn_widget_t *w)
{
    check_t *c = w->data;
    free(c->label);
    free(c);
}

static const idunn_class_t check_class = {
    .name = "check box",
    .measure = check_measure,
    .draw = check_draw,
    .event = check_event,
    .free = check_free,
};

idunn_widget_t *idunn_check_box(const char *label, bool checked, idunn_callback_t on_change,
                                void *data)
{
    check_t *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->label = label ? strdup(label) : NULL;
    c->state = checked ? IDUNN_CHECKED : IDUNN_UNCHECKED;
    c->on_change = on_change;
    c->data = data;
    idunn_widget_t *w = idunn_widget_new(&check_class, c, IDUNN_FOCUSABLE | IDUNN_INTERACTIVE);
    if (!w)
    {
        free(c->label);
        free(c);
    }
    return w;
}

static check_t *as_check(const idunn_widget_t *w)
{
    return w && w->cls == &check_class ? w->data : NULL;
}

idunn_check_t idunn_check_box_state(const idunn_widget_t *box)
{
    const check_t *c = as_check(box);
    return c ? c->state : IDUNN_UNCHECKED;
}

void idunn_check_box_set_state(idunn_widget_t *box, idunn_check_t state)
{
    check_t *c = as_check(box);
    if (!c)
        return;
    c->state = state;
    idunn_dirty(box);
}
