/**
 * @file separator.c
 * @brief idunn: a 1 pixel line in the border colour, across a column or
 *        down a row (its box stretches it).
 */

#include "idunn_internal.h"

static void separator_measure(idunn_widget_t *w, int32_t max_width, int32_t *width,
                              int32_t *height)
{
    (void)max_width;
    int32_t one = idunn_px(idunn_theme(w), 1);
    bool across = !w->parent || idunn_is_column(w->parent);
    *width = across ? 0 : one;
    *height = across ? one : 0;
}

static void separator_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    gfx_fill_rect(buf, w->rect, idunn_color(idunn_theme(w), IDUNN_COLOR_BORDER));
}

static const idunn_class_t separator_class = {
    .name = "separator",
    .measure = separator_measure,
    .draw = separator_draw,
};

idunn_widget_t *idunn_separator(void)
{
    return idunn_widget_new(&separator_class, NULL, 0);
}
