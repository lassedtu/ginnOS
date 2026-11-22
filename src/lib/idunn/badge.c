/**
 * @file badge.c
 * @brief idunn: a badge: small medium text in a pill, 22 pixels high,
 *        in four variants (default, secondary, outline, destructive).
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define HEIGHT 22
#define PAD    8

typedef struct
{
    char *text;
    idunn_badge_t variant;
} badge_t;

static void badge_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)max_width;
    const badge_t *b = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    bragi_face_t *face = idunn_face(idunn_ctx(w), IDUNN_TEXT_CAPTION);
    int32_t length = (int32_t)strlen(b->text);
    *width = 2 * idunn_px(t, PAD + 1) + (face ? bragi_caret_x(face, b->text, length, length) : 0);
    *height = idunn_px(t, HEIGHT);
}

static void badge_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const badge_t *b = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    gfx_rect_t r = w->rect;
    int32_t round = r.h / 2;
    gfx_color_t fill = 0;
    gfx_color_t text = idunn_color(t, IDUNN_COLOR_FOREGROUND);
    switch (b->variant)
    {
    case IDUNN_BADGE_SECONDARY:
        fill = idunn_color(t, IDUNN_COLOR_SECONDARY);
        text = idunn_color(t, IDUNN_COLOR_SECONDARY_FOREGROUND);
        break;
    case IDUNN_BADGE_OUTLINE:
        gfx_border_round_rect(buf, r, round, idunn_px(t, 1), idunn_color(t, IDUNN_COLOR_BORDER));
        break;
    case IDUNN_BADGE_DESTRUCTIVE:
        fill = idunn_color(t, IDUNN_COLOR_DESTRUCTIVE);
        text = gfx_rgb(255, 255, 255);
        break;
    default:
        fill = idunn_color(t, IDUNN_COLOR_PRIMARY);
        text = idunn_color(t, IDUNN_COLOR_PRIMARY_FOREGROUND);
        break;
    }
    if (fill)
        gfx_fill_round_rect(buf, r, round, fill);
    int32_t line = idunn_text_style(t, IDUNN_TEXT_CAPTION).line_height;
    idunn_draw_text(ctx, buf, IDUNN_TEXT_CAPTION, r.x + idunn_px(t, PAD + 1),
                    r.y + (r.h - line) / 2, 0, b->text, text);
}

static void badge_free(idunn_widget_t *w)
{
    badge_t *b = w->data;
    free(b->text);
    free(b);
}

static const idunn_class_t badge_class = {
    .name = "badge",
    .measure = badge_measure,
    .draw = badge_draw,
    .free = badge_free,
};

idunn_widget_t *idunn_badge(const char *text, idunn_badge_t variant)
{
    badge_t *b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    b->text = strdup(text ? text : "");
    b->variant = variant;
    idunn_widget_t *w = b->text ? idunn_widget_new(&badge_class, b, 0) : NULL;
    if (!w)
    {
        free(b->text);
        free(b);
    }
    return w;
}
