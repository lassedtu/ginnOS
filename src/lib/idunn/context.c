/**
 * @file context.c
 * @brief idunn: the fonts, text faces and icons of a theme.
 */

#include "idunn_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct icon_entry
{
    char name[ICON_NAME_MAX + 1];
    int32_t size;
    icon_t *icon;
    struct icon_entry *next;
};

static const char *const font_files[4] = {
    "inter/Inter-Regular.ttf",
    "inter/Inter-Medium.ttf",
    "inter/Inter-SemiBold.ttf",
    "jetbrains-mono/JetBrainsMono-Regular.ttf",
};

static void free_faces(idunn_context_t *ctx)
{
    for (int i = 0; i < IDUNN_TEXT_COUNT; i++)
    {
        bragi_face_free(ctx->faces[i]);
        ctx->faces[i] = NULL;
    }
}

static void free_icons(idunn_context_t *ctx)
{
    while (ctx->icon_cache)
    {
        struct icon_entry *next = ctx->icon_cache->next;
        icon_free(ctx->icon_cache->icon);
        free(ctx->icon_cache);
        ctx->icon_cache = next;
    }
}

idunn_context_t *idunn_context_new(const idunn_theme_t *theme, const char *font_dir,
                                   const char *icon_pack)
{
    idunn_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    ctx->theme = *theme;
    for (int i = 0; i < 4; i++)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", font_dir, font_files[i]);
        ctx->fonts[i] = bragi_font_open(path);
        if (!ctx->fonts[i])
        {
            idunn_context_free(ctx);
            return NULL;
        }
    }
    ctx->icons = icon_pack ? icon_set_open(icon_pack) : NULL;
    return ctx;
}

void idunn_context_free(idunn_context_t *ctx)
{
    if (!ctx)
        return;
    free_faces(ctx);
    free_icons(ctx);
    for (int i = 0; i < 4; i++)
        bragi_font_close(ctx->fonts[i]);
    icon_set_close(ctx->icons);
    free(ctx);
}

void idunn_context_set_theme(idunn_context_t *ctx, const idunn_theme_t *theme)
{
    // the sizes of text and icons can change with the theme
    free_faces(ctx);
    free_icons(ctx);
    ctx->theme = *theme;
}

bragi_face_t *idunn_face(idunn_context_t *ctx, idunn_text_t style)
{
    if (style < 0 || style >= IDUNN_TEXT_COUNT)
        style = IDUNN_TEXT_BODY;
    if (!ctx->faces[style])
    {
        idunn_text_style_t s = idunn_text_style(&ctx->theme, style);
        int font = s.mono ? 3 : s.weight >= 600 ? 2 : s.weight >= 500 ? 1 : 0;
        ctx->faces[style] = bragi_face_new(ctx->fonts[font], (float)s.size);
        if (ctx->faces[style])
            bragi_face_set_tracking(ctx->faces[style], s.tracking);
    }
    return ctx->faces[style];
}

const icon_t *idunn_icon(idunn_context_t *ctx, const char *name, int32_t size)
{
    if (!ctx->icons || !name)
        return NULL;
    for (struct icon_entry *e = ctx->icon_cache; e; e = e->next)
    {
        if (e->size == size && strcmp(e->name, name) == 0)
            return e->icon;
    }
    struct icon_entry *e = calloc(1, sizeof(*e));
    if (!e)
        return NULL;
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->size = size;
    e->icon = icon_load(ctx->icons, name, size, ctx->theme.scale);
    e->next = ctx->icon_cache;
    ctx->icon_cache = e;
    return e->icon;
}

void idunn_draw_text(idunn_context_t *ctx, gfx_buffer_t *buf, idunn_text_t style, int32_t x,
                     int32_t y, int32_t max_width, const char *text, gfx_color_t color)
{
    bragi_face_t *face = idunn_face(ctx, style);
    if (!face || !text)
        return;
    int32_t baseline = y + bragi_line_baseline(face, idunn_text_style(&ctx->theme, style).line_height);
    int32_t length = (int32_t)strlen(text);
    if (max_width <= 0 || bragi_measure(face, text, length) <= max_width)
    {
        bragi_draw(buf, face, x, baseline, text, length, color);
        return;
    }
    // as many whole characters as fit before the "..."
    static const char dots[] = "\xe2\x80\xa6";
    int32_t room = max_width - bragi_measure(face, dots, -1);
    int32_t fit = bragi_offset_at(face, text, length, room);
    while (fit > 0 && bragi_caret_x(face, text, length, fit) > room)
    {
        do
            fit--;
        while (fit > 0 && ((unsigned char)text[fit] & 0xC0) == 0x80);
    }
    int32_t w = bragi_draw(buf, face, x, baseline, text, fit, color);
    bragi_draw(buf, face, x + w, baseline, dots, -1, color);
}

int32_t idunn_overdraw(const idunn_theme_t *theme)
{
    return idunn_px(theme, 4);
}

void idunn_draw_ring(const idunn_widget_t *w, gfx_buffer_t *buf, gfx_rect_t box, int32_t radius)
{
    const idunn_theme_t *t = idunn_theme(w);
    gfx_shadow_t ring = {0, 0, 0, idunn_px(t, 3), idunn_color(t, IDUNN_COLOR_RING), false};
    gfx_box_shadow(buf, box, radius, &ring);
}
