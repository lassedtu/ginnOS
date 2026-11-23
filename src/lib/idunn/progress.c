/**
 * @file progress.c
 * @brief idunn: a progress bar: a rounded track in the muted colour, filled
 *        from the left in the accent colour, 8 pixels high. its box
 *        stretches it across.
 */

#include "idunn_internal.h"

#include <stdlib.h>

#define HEIGHT 8

typedef struct
{
    int32_t value; // 0 to IDUNN_PROGRESS_MAX
} progress_t;

static void progress_measure(idunn_widget_t *w, int32_t max_width, int32_t *width,
                             int32_t *height)
{
    (void)max_width;
    *width = 0;
    *height = idunn_px(idunn_theme(w), HEIGHT);
}

static void progress_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const idunn_theme_t *t = idunn_theme(w);
    const progress_t *p = w->data;
    gfx_rect_t track = w->rect;
    int32_t radius = track.h / 2;
    gfx_fill_round_rect(buf, track, radius, idunn_color(t, IDUNN_COLOR_MUTED));
    gfx_rect_t filled = track;
    filled.w = (int32_t)((int64_t)track.w * p->value / IDUNN_PROGRESS_MAX);
    // a sliver shorter than the round end shows as a dot of the round end
    if (filled.w > 0 && filled.w < track.h)
        filled.w = track.h;
    if (filled.w > 0)
        gfx_fill_round_rect(buf, filled, radius, idunn_color(t, IDUNN_COLOR_ACCENT));
}

static void progress_free(idunn_widget_t *w)
{
    free(w->data);
}

static const idunn_class_t progress_class = {
    .name = "progress",
    .measure = progress_measure,
    .draw = progress_draw,
    .free = progress_free,
};

idunn_widget_t *idunn_progress(void)
{
    progress_t *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    idunn_widget_t *w = idunn_widget_new(&progress_class, p, 0);
    if (!w)
        free(p);
    return w;
}

void idunn_progress_set(idunn_widget_t *progress, int32_t value)
{
    if (!progress || progress->cls != &progress_class)
        return;
    progress_t *p = progress->data;
    value = value < 0 ? 0 : value > IDUNN_PROGRESS_MAX ? IDUNN_PROGRESS_MAX : value;
    if (p->value == value)
        return;
    p->value = value;
    idunn_dirty(progress);
}
