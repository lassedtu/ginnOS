/**
 * @file radio.c
 * @brief idunn: a radio group: choices with 16 pixel circles, one of
 *        them chosen (an accent ring and an 8 pixel dot). the group is one
 *        stop for Tab; the arrows move the choice.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define SIZE     16
#define DOT      8
#define GAP_COL  8  // between choices in a column
#define GAP_ROW  16 // in a row

typedef struct
{
    char **labels;
    int count;
    int chosen;
    bool row;
    idunn_callback_t on_change;
    void *data;
} radio_t;

/** the size of one choice */
static void choice_size(idunn_widget_t *w, int i, int32_t *width, int32_t *height)
{
    const radio_t *r = w->data;
    idunn_option_measure(w, SIZE, SIZE, r->labels[i], width, height);
}

static int32_t gap_of(const idunn_widget_t *w)
{
    const radio_t *r = w->data;
    return idunn_px(idunn_theme(w), r->row ? GAP_ROW : GAP_COL);
}

static void radio_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)max_width;
    const radio_t *r = w->data;
    *width = 0;
    *height = 0;
    for (int i = 0; i < r->count; i++)
    {
        int32_t cw;
        int32_t ch;
        choice_size(w, i, &cw, &ch);
        int32_t gap = i ? gap_of(w) : 0;
        if (r->row)
        {
            *width += gap + cw;
            *height = ch > *height ? ch : *height;
        }
        else
        {
            *height += gap + ch;
            *width = cw > *width ? cw : *width;
        }
    }
}

/** the rect of choice i in the widget */
static gfx_rect_t choice_rect(idunn_widget_t *w, int i)
{
    const radio_t *r = w->data;
    int32_t x = w->rect.x;
    int32_t y = w->rect.y;
    int32_t cw = 0;
    int32_t ch = 0;
    for (int k = 0; k <= i; k++)
    {
        if (k)
        {
            if (r->row)
                x += cw + gap_of(w);
            else
                y += ch + gap_of(w);
        }
        choice_size(w, k, &cw, &ch);
    }
    return gfx_rect(x, y, cw, ch);
}

static void radio_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const radio_t *r = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    int32_t size = idunn_px(t, SIZE);
    int32_t line = idunn_text_style(t, IDUNN_TEXT_BODY).line_height;
    for (int i = 0; i < r->count; i++)
    {
        gfx_rect_t c = choice_rect(w, i);
        gfx_rect_t circle = gfx_rect(c.x, c.y + (c.h - size) / 2, size, size);
        bool on = i == r->chosen;
        if (on && idunn_ring_visible(w))
            idunn_draw_ring(w, buf, circle, size / 2);
        idunn_draw_shadow(t, buf, circle, size / 2, IDUNN_SHADOW_XS);
        gfx_border_round_rect(buf, circle, size / 2, idunn_px(t, 1),
                              on ? idunn_color(t, IDUNN_COLOR_ACCENT) : idunn_option_border(t));
        if (on)
        {
            int32_t dot = idunn_px(t, DOT);
            gfx_fill_round_rect(buf,
                                gfx_rect(circle.x + (size - dot) / 2, circle.y + (size - dot) / 2,
                                         dot, dot),
                                dot / 2, idunn_color(t, IDUNN_COLOR_ACCENT));
        }
        int32_t x = c.x + idunn_px(t, SIZE + 10);
        idunn_draw_text(ctx, buf, IDUNN_TEXT_BODY, x, c.y + (c.h - line) / 2, 0, r->labels[i],
                        idunn_color(t, IDUNN_COLOR_FOREGROUND));
    }
}

static void choose(idunn_widget_t *w, int i)
{
    radio_t *r = w->data;
    if (i < 0 || i >= r->count || i == r->chosen)
        return;
    r->chosen = i;
    idunn_dirty(w);
    if (r->on_change)
        r->on_change(w, r->data);
}

static bool radio_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    radio_t *r = w->data;
    if (ev->type == IDUNN_EV_PRESS)
    {
        for (int i = 0; i < r->count; i++)
        {
            if (gfx_rect_contains(choice_rect(w, i), ev->x, ev->y))
                choose(w, i);
        }
        return true;
    }
    if (ev->type != IDUNN_EV_KEY)
        return false;
    switch (ev->code)
    {
    case IDUNN_KEY_UP:
    case IDUNN_KEY_LEFT:
        choose(w, r->chosen > 0 ? r->chosen - 1 : r->count - 1);
        return true;
    case IDUNN_KEY_DOWN:
    case IDUNN_KEY_RIGHT:
        choose(w, r->chosen + 1 < r->count ? r->chosen + 1 : 0);
        return true;
    case IDUNN_KEY_SPACE:
        choose(w, r->chosen < 0 ? 0 : r->chosen);
        return true;
    default:
        return false;
    }
}

static void free_radio(radio_t *r)
{
    for (int i = 0; r->labels && i < r->count; i++)
        free(r->labels[i]);
    free(r->labels);
    free(r);
}

static void radio_free(idunn_widget_t *w)
{
    free_radio(w->data);
}

static const idunn_class_t radio_class = {
    .name = "radio group",
    .measure = radio_measure,
    .draw = radio_draw,
    .event = radio_event,
    .free = radio_free,
};

idunn_widget_t *idunn_radio_group(const char *const *labels, int count, int chosen,
                                  idunn_callback_t on_change, void *data)
{
    radio_t *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    r->labels = calloc((size_t)(count > 0 ? count : 1), sizeof(char *));
    for (int i = 0; r->labels && i < count; i++)
        r->labels[i] = strdup(labels[i] ? labels[i] : "");
    r->count = r->labels ? count : 0;
    r->chosen = chosen < count ? chosen : -1;
    r->on_change = on_change;
    r->data = data;
    idunn_widget_t *w = idunn_widget_new(&radio_class, r, IDUNN_FOCUSABLE | IDUNN_INTERACTIVE);
    if (!w)
    {
        free_radio(r);
        return NULL;
    }
    return w;
}

static radio_t *as_radio(const idunn_widget_t *w)
{
    return w && w->cls == &radio_class ? w->data : NULL;
}

int idunn_radio_group_chosen(const idunn_widget_t *group)
{
    const radio_t *r = as_radio(group);
    return r ? r->chosen : -1;
}

void idunn_radio_group_choose(idunn_widget_t *group, int chosen)
{
    radio_t *r = as_radio(group);
    if (!r)
        return;
    r->chosen = chosen >= 0 && chosen < r->count ? chosen : -1;
    idunn_dirty(group);
}

void idunn_radio_group_set_row(idunn_widget_t *group, bool row)
{
    radio_t *r = as_radio(group);
    if (!r)
        return;
    r->row = row;
    idunn_relayout(group);
}
