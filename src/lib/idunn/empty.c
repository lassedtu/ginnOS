/**
 * @file empty.c
 * @brief idunn: an icon on its own, and the empty state: an icon in a
 *        muted square, a title and a muted line, in the middle of the space.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define TILE 40 // the muted square of an empty state
#define GLYPH 20

typedef struct
{
    char *name;
    int32_t size;
    idunn_color_t color;
    bool tile; // on a muted square
} icon_view_t;

static int32_t outer(const idunn_widget_t *w)
{
    const icon_view_t *v = w->data;
    return idunn_px(idunn_theme(w), v->tile ? TILE : v->size);
}

static void icon_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)max_width;
    *width = outer(w);
    *height = outer(w);
}

static void view_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const icon_view_t *v = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    int32_t box = outer(w);
    int32_t x = w->rect.x + (w->rect.w - box) / 2;
    int32_t y = w->rect.y + (w->rect.h - box) / 2;
    if (v->tile)
        gfx_fill_round_rect(buf, gfx_rect(x, y, box, box), idunn_radius_lg(t),
                            idunn_color(t, IDUNN_COLOR_MUTED));
    const icon_t *icon = idunn_icon(ctx, v->name, v->size);
    int32_t size = idunn_px(t, v->size);
    if (icon)
        icon_draw_mask(buf, icon, x + (box - size) / 2, y + (box - size) / 2,
                       idunn_color(t, v->color));
}

static void view_free(idunn_widget_t *w)
{
    icon_view_t *v = w->data;
    free(v->name);
    free(v);
}

static const idunn_class_t icon_class = {
    .name = "icon",
    .measure = icon_measure,
    .draw = view_draw,
    .free = view_free,
};

static idunn_widget_t *icon_new(const char *name, int32_t size, idunn_color_t color, bool tile)
{
    icon_view_t *v = calloc(1, sizeof(*v));
    if (!v)
        return NULL;
    v->name = strdup(name ? name : "");
    v->size = size > 0 ? size : 16;
    v->color = color;
    v->tile = tile;
    idunn_widget_t *w = v->name ? idunn_widget_new(&icon_class, v, 0) : NULL;
    if (!w)
    {
        free(v->name);
        free(v);
    }
    return w;
}

idunn_widget_t *idunn_icon_view(const char *name, int32_t size, idunn_color_t color)
{
    return icon_new(name, size, color, false);
}

idunn_widget_t *idunn_empty_state(const char *icon, const char *title, const char *text)
{
    idunn_widget_t *col = idunn_column(2, 6);
    if (!col)
        return NULL;
    idunn_box_set_align(col, IDUNN_CENTER, IDUNN_CENTER);
    idunn_add(col, icon_new(icon, GLYPH, IDUNN_COLOR_MUTED_FOREGROUND, true));
    idunn_widget_t *gap = idunn_add(col, idunn_spacer());
    gap->grow = 0;
    idunn_set_size(gap, 0, 4);
    idunn_add(col, idunn_label(title ? title : "", IDUNN_TEXT_TITLE));
    if (text)
    {
        idunn_widget_t *line = idunn_add(col, idunn_label(text, IDUNN_TEXT_BODY));
        idunn_label_set_color(line, IDUNN_COLOR_MUTED_FOREGROUND);
    }
    return col;
}
