/**
 * @file list_heads.c
 * @brief idunn: heads over the columns of a list view.
 *
 * the heads read the columns' places from the list each time they are
 * drawn, so they line up with it at every width. the names' head is at
 * the left; the other heads are at the right of their columns, as the
 * numbers in them. the head of the order has an arrow.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define HEIGHT     26 // design pixels
#define HEAD_MAX   9  // the names and COLUMNS_MAX columns
#define ARROW      12
#define ARROW_GAP  4

typedef struct
{
    idunn_widget_t *list; // NULL after the list is freed
    char *names[HEAD_MAX];
    int count;
    int order; // the head with the arrow, or -1
    bool descending;
    int hover;   // the head under the pointer, or -1
    int pressed; // the head the button went down on, or -1
    int clicked;
    int32_t row_x; // where the rows start (parts sets it)
    idunn_callback_t on_click;
    void *data;
} heads_t;

/** the part of each head that can be clicked, in the window */
static int parts(idunn_widget_t *w, gfx_rect_t *out)
{
    heads_t *h = w->data;
    if (!h->list)
        return 0;
    int32_t name_x;
    int32_t row_x;
    int32_t left[HEAD_MAX];
    int32_t right[HEAD_MAX];
    int columns = idunn_list_columns_at(h->list, &name_x, &row_x, left, right);
    h->row_x = row_x;
    const idunn_theme_t *t = idunn_theme(w);
    int32_t pad = idunn_px(t, 6);
    int count = h->count < columns + 1 ? h->count : columns + 1;
    int32_t name_end = columns ? left[0] - pad : w->rect.x + w->rect.w;
    out[0] = gfx_rect(name_x - pad, w->rect.y, name_end - (name_x - pad), w->rect.h);
    for (int c = 1; c < count; c++)
        out[c] = gfx_rect(left[c - 1] - pad, w->rect.y, right[c - 1] - left[c - 1] + 2 * pad,
                          w->rect.h);
    return count;
}

static int head_at(idunn_widget_t *w, int32_t x, int32_t y)
{
    gfx_rect_t r[HEAD_MAX];
    int count = parts(w, r);
    for (int i = 0; i < count; i++)
        if (gfx_rect_contains(r[i], x, y))
            return i;
    return -1;
}

static void heads_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    *width = max_width > 0 ? max_width : 0;
    *height = idunn_px(idunn_theme(w), HEIGHT);
}

static void heads_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    heads_t *h = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    gfx_rect_t r[HEAD_MAX];
    int count = parts(w, r);
    bragi_face_t *face = idunn_face(ctx, IDUNN_TEXT_SMALL);
    int32_t line = idunn_text_style(t, IDUNN_TEXT_SMALL).line_height;
    int32_t pad = idunn_px(t, 6);
    int32_t arrow = idunn_px(t, ARROW);
    // a line under the heads, as wide as the rows
    if (count)
    {
        int32_t one = idunn_px(t, 1);
        int32_t end = r[count - 1].x + r[count - 1].w;
        gfx_fill_rect(buf, gfx_rect(h->row_x, w->rect.y + w->rect.h - one, end - h->row_x, one),
                      idunn_color(t, IDUNN_COLOR_BORDER));
    }
    for (int i = 0; i < count; i++)
    {
        if (i == h->hover && (w->state & IDUNN_HOVER))
            gfx_fill_round_rect(buf, r[i], idunn_px(t, 4), idunn_color(t, IDUNN_COLOR_HOVER));
        bool ordered = i == h->order;
        gfx_color_t color = idunn_color(t, ordered ? IDUNN_COLOR_FOREGROUND
                                                   : IDUNN_COLOR_MUTED_FOREGROUND);
        int32_t tw = face ? bragi_measure(face, h->names[i], -1) : 0;
        int32_t room = r[i].w - 2 * pad - (ordered ? arrow + idunn_px(t, ARROW_GAP) : 0);
        tw = tw < room ? tw : room;
        // at the left as the names and text columns, arrow after it; numbers'
        // heads at the right, arrow before it
        bool left = i == 0 || idunn_list_column_left(h->list, i - 1);
        int32_t x = left ? r[i].x + pad : r[i].x + r[i].w - pad - tw;
        int32_t top = r[i].y + (r[i].h - line) / 2;
        idunn_draw_text(ctx, buf, IDUNN_TEXT_SMALL, x, top, room, h->names[i], color);
        if (!ordered)
            continue;
        const icon_t *icon = idunn_icon(ctx, h->descending ? "chevron-down" : "chevron-up", ARROW);
        int32_t ax = left ? x + tw + idunn_px(t, ARROW_GAP) : x - idunn_px(t, ARROW_GAP) - arrow;
        if (icon)
            icon_draw_mask(buf, icon, ax, r[i].y + (r[i].h - arrow) / 2, color);
    }
}

static bool heads_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    heads_t *h = w->data;
    switch (ev->type)
    {
    case IDUNN_EV_MOTION:
    {
        int at = head_at(w, ev->x, ev->y);
        if (at != h->hover)
        {
            h->hover = at;
            idunn_dirty(w);
        }
        return true;
    }
    case IDUNN_EV_PRESS:
        h->pressed = head_at(w, ev->x, ev->y);
        return h->pressed >= 0;
    case IDUNN_EV_RELEASE:
    {
        int at = head_at(w, ev->x, ev->y);
        if (at >= 0 && at == h->pressed)
        {
            h->clicked = at;
            if (h->on_click)
                h->on_click(w, h->data);
        }
        h->pressed = -1;
        return true;
    }
    default:
        return false;
    }
}

static void heads_free(idunn_widget_t *w)
{
    heads_t *h = w->data;
    if (h->list)
        idunn_list_set_heads(h->list, NULL);
    for (int i = 0; i < h->count; i++)
        free(h->names[i]);
    free(h);
}

static const idunn_class_t heads_class = {
    .name = "list heads",
    .measure = heads_measure,
    .draw = heads_draw,
    .event = heads_event,
    .free = heads_free,
};

static heads_t *as_heads(const idunn_widget_t *w)
{
    return w && w->cls == &heads_class ? w->data : NULL;
}

idunn_widget_t *idunn_list_heads(idunn_widget_t *list, const char *const *names, int count,
                                 idunn_callback_t on_click, void *data)
{
    heads_t *h = calloc(1, sizeof(*h));
    idunn_widget_t *w = h ? idunn_widget_new(&heads_class, h, IDUNN_INTERACTIVE) : NULL;
    if (!w)
    {
        free(h);
        return NULL;
    }
    h->list = list;
    h->count = count < HEAD_MAX ? count : HEAD_MAX;
    for (int i = 0; i < h->count; i++)
        h->names[i] = strdup(names[i] ? names[i] : "");
    h->order = -1;
    h->hover = -1;
    h->pressed = -1;
    h->clicked = -1;
    h->on_click = on_click;
    h->data = data;
    idunn_list_set_heads(list, w);
    return w;
}

void idunn_list_heads_forget(idunn_widget_t *heads)
{
    heads_t *h = as_heads(heads);
    if (h)
        h->list = NULL;
}

int idunn_list_heads_clicked(const idunn_widget_t *heads)
{
    const heads_t *h = as_heads(heads);
    return h ? h->clicked : -1;
}

void idunn_list_heads_set_order(idunn_widget_t *heads, int head, bool descending)
{
    heads_t *h = as_heads(heads);
    if (!h)
        return;
    h->order = head;
    h->descending = descending;
    idunn_dirty(heads);
}
