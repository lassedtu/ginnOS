/**
 * @file option.c
 * @brief idunn: a small control with its label to the right (check box,
 *        radio, switch), 10 pixels apart as in the preview. a click on the
 *        label is a click on the control.
 */

#include "idunn_internal.h"

#define GAP 10

void idunn_option_measure(idunn_widget_t *w, int32_t control_w, int32_t control_h,
                          const char *label, int32_t *width, int32_t *height)
{
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    *width = idunn_px(t, control_w);
    *height = idunn_px(t, control_h);
    if (label && *label)
    {
        bragi_face_t *face = idunn_face(ctx, IDUNN_TEXT_BODY);
        int32_t line = idunn_text_style(t, IDUNN_TEXT_BODY).line_height;
        *width += idunn_px(t, GAP) + (face ? bragi_measure(face, label, -1) : 0);
        if (line > *height)
            *height = line;
    }
}

gfx_rect_t idunn_option_control(const idunn_widget_t *w, int32_t control_w, int32_t control_h)
{
    const idunn_theme_t *t = idunn_theme(w);
    int32_t cw = idunn_px(t, control_w);
    int32_t ch = idunn_px(t, control_h);
    return gfx_rect(w->rect.x, w->rect.y + (w->rect.h - ch) / 2, cw, ch);
}

void idunn_option_draw_label(idunn_widget_t *w, gfx_buffer_t *buf, int32_t control_w,
                             const char *label)
{
    if (!label || !*label)
        return;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    int32_t line = idunn_text_style(t, IDUNN_TEXT_BODY).line_height;
    int32_t x = w->rect.x + idunn_px(t, control_w + GAP);
    idunn_draw_text(ctx, buf, IDUNN_TEXT_BODY, x, w->rect.y + (w->rect.h - line) / 2,
                    w->rect.x + w->rect.w - x, label, idunn_color(t, IDUNN_COLOR_FOREGROUND));
}

gfx_color_t idunn_option_border(const idunn_theme_t *t)
{
    // check boxes and radios have a firmer border than fields in dark mode
    return t->mode == IDUNN_DARK ? gfx_rgba(255, 255, 255, 64) : idunn_color(t, IDUNN_COLOR_INPUT);
}

void idunn_draw_shadow(const idunn_theme_t *t, gfx_buffer_t *buf, gfx_rect_t box, int32_t radius,
                       idunn_shadow_t which)
{
    gfx_shadow_t layers[IDUNN_SHADOW_LAYERS];
    int count = idunn_shadow(t, which, layers);
    for (int i = 0; i < count; i++)
        gfx_box_shadow(buf, box, radius, &layers[i]);
}
