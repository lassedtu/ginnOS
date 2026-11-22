/**
 * @file label.c
 * @brief idunn: text in a style. one line ends with "..." when it is too
 *        wide; a wrapping label breaks its lines at spaces.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *text;
    idunn_text_t style;
    idunn_color_t color;
    bool wrap;
} label_t;

/**
 * the lines of text in a width; with draw, they are drawn from (x, y).
 * @return the number of lines.
 */
static int32_t lines(idunn_widget_t *w, int32_t width, gfx_buffer_t *buf, int32_t x, int32_t y)
{
    const label_t *l = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    bragi_face_t *face = idunn_face(ctx, l->style);
    int32_t line_height = idunn_text_style(&ctx->theme, l->style).line_height;
    gfx_color_t color = idunn_color(&ctx->theme, l->color);
    const char *s = l->text;
    int32_t count = 0;
    do
    {
        const char *next;
        int32_t length = bragi_wrap(face, s, width, &next);
        if (buf)
        {
            char *line = strndup(s, (size_t)length);
            if (line)
                idunn_draw_text(ctx, buf, l->style, x, y + count * line_height, 0, line, color);
            free(line);
        }
        count++;
        s = next;
    } while (*s);
    return count;
}

static void label_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    const label_t *l = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    bragi_face_t *face = idunn_face(ctx, l->style);
    int32_t line_height = idunn_text_style(&ctx->theme, l->style).line_height;
    *width = face ? bragi_measure(face, l->text, -1) : 0;
    *height = line_height;
    if (l->wrap && face && max_width > 0 && *width > max_width)
    {
        *width = max_width;
        *height = lines(w, max_width, NULL, 0, 0) * line_height;
    }
}

static void label_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const label_t *l = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    if (l->wrap)
    {
        lines(w, w->rect.w, buf, w->rect.x, w->rect.y);
        return;
    }
    idunn_draw_text(ctx, buf, l->style, w->rect.x, w->rect.y, w->rect.w, l->text,
                    idunn_color(&ctx->theme, l->color));
}

static void label_free(idunn_widget_t *w)
{
    label_t *l = w->data;
    free(l->text);
    free(l);
}

static const idunn_class_t label_class = {
    .name = "label",
    .measure = label_measure,
    .draw = label_draw,
    .free = label_free,
};

idunn_widget_t *idunn_label(const char *text, idunn_text_t style)
{
    label_t *l = calloc(1, sizeof(*l));
    if (!l)
        return NULL;
    l->text = strdup(text ? text : "");
    l->style = style;
    l->color = IDUNN_COLOR_FOREGROUND;
    idunn_widget_t *w = l->text ? idunn_widget_new(&label_class, l, 0) : NULL;
    if (!w)
    {
        free(l->text);
        free(l);
    }
    return w;
}

static label_t *as_label(idunn_widget_t *w)
{
    return w && w->cls == &label_class ? w->data : NULL;
}

void idunn_label_set_text(idunn_widget_t *label, const char *text)
{
    label_t *l = as_label(label);
    char *copy = l ? strdup(text ? text : "") : NULL;
    if (!copy)
        return;
    free(l->text);
    l->text = copy;
    idunn_dirty(label);
    idunn_relayout(label);
}

void idunn_label_set_color(idunn_widget_t *label, idunn_color_t color)
{
    label_t *l = as_label(label);
    if (!l)
        return;
    l->color = color;
    idunn_dirty(label);
}

void idunn_label_set_wrap(idunn_widget_t *label, bool wrap)
{
    label_t *l = as_label(label);
    if (!l)
        return;
    l->wrap = wrap;
    idunn_relayout(label);
}
