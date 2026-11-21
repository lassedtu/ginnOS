/**
 * @file gfx.c
 * @brief libgfx: colours, rectangles, buffers, fills, copies and output.
 */

#include "gfx_internal.h"

#include <stdlib.h>
#include <string.h>

/* ---- colours -------------------------------------------------------------- */

gfx_color_t gfx_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return gfx_scale_pixel(0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b, a);
}

gfx_color_t gfx_fade(gfx_color_t c, uint8_t opacity)
{
    return gfx_scale_pixel(c, opacity);
}

gfx_color_t gfx_over(gfx_color_t dst, gfx_color_t src)
{
    return gfx_over_pixel(dst, src);
}

/* ---- rectangles ----------------------------------------------------------- */

gfx_rect_t gfx_rect_intersect(gfx_rect_t a, gfx_rect_t b)
{
    int32_t x0 = a.x > b.x ? a.x : b.x;
    int32_t y0 = a.y > b.y ? a.y : b.y;
    int32_t x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int32_t y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    if (x1 <= x0 || y1 <= y0)
        return gfx_rect(0, 0, 0, 0);
    return gfx_rect(x0, y0, x1 - x0, y1 - y0);
}

gfx_rect_t gfx_rect_union(gfx_rect_t a, gfx_rect_t b)
{
    if (gfx_rect_empty(a))
        return b;
    if (gfx_rect_empty(b))
        return a;
    int32_t x0 = a.x < b.x ? a.x : b.x;
    int32_t y0 = a.y < b.y ? a.y : b.y;
    int32_t x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int32_t y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return gfx_rect(x0, y0, x1 - x0, y1 - y0);
}

bool gfx_rect_contains(gfx_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* ---- buffers -------------------------------------------------------------- */

gfx_buffer_t *gfx_buffer_new(int32_t width, int32_t height)
{
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
        return NULL;
    gfx_buffer_t *buf = malloc(sizeof(*buf));
    uint32_t *pixels = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
    if (!buf || !pixels)
    {
        free(buf);
        free(pixels);
        return NULL;
    }
    gfx_buffer_init(buf, pixels, width, height, width);
    buf->owned = true;
    return buf;
}

void gfx_buffer_free(gfx_buffer_t *buf)
{
    if (!buf)
        return;
    if (buf->owned)
        free(buf->pixels);
    free(buf);
}

void gfx_buffer_init(gfx_buffer_t *buf, uint32_t *pixels, int32_t width, int32_t height,
                     int32_t stride)
{
    buf->pixels = pixels;
    buf->width = width;
    buf->height = height;
    buf->stride = stride;
    buf->clip = gfx_rect(0, 0, width, height);
    buf->owned = false;
}

gfx_buffer_t gfx_buffer_sub(const gfx_buffer_t *parent, gfx_rect_t r)
{
    gfx_buffer_t sub;
    r = gfx_rect_intersect(r, gfx_rect(0, 0, parent->width, parent->height));
    gfx_buffer_init(&sub, gfx_rect_empty(r) ? parent->pixels : gfx_pixel_at(parent, r.x, r.y),
                    r.w > 0 ? r.w : 0, r.h > 0 ? r.h : 0, parent->stride);
    return sub;
}

void gfx_set_clip(gfx_buffer_t *buf, gfx_rect_t clip)
{
    buf->clip = gfx_rect_intersect(clip, gfx_rect(0, 0, buf->width, buf->height));
}

void gfx_reset_clip(gfx_buffer_t *buf)
{
    buf->clip = gfx_rect(0, 0, buf->width, buf->height);
}

/* ---- fills ---------------------------------------------------------------- */

static void fill_span(uint32_t *row, int32_t count, gfx_color_t color)
{
    uint32_t a = color >> 24;
    if (a == 255)
    {
        for (int32_t i = 0; i < count; i++)
            row[i] = color;
    }
    else if (a != 0)
    {
        for (int32_t i = 0; i < count; i++)
            row[i] = color + gfx_scale_pixel(row[i], 255 - a);
    }
}

void gfx_clear(gfx_buffer_t *buf, gfx_color_t color)
{
    gfx_rect_t c = gfx_clip_of(buf);
    for (int32_t y = c.y; y < c.y + c.h; y++)
    {
        uint32_t *row = gfx_pixel_at(buf, c.x, y);
        for (int32_t i = 0; i < c.w; i++)
            row[i] = color;
    }
}

void gfx_fill_rect(gfx_buffer_t *buf, gfx_rect_t r, gfx_color_t color)
{
    r = gfx_rect_intersect(r, gfx_clip_of(buf));
    for (int32_t y = r.y; y < r.y + r.h; y++)
        fill_span(gfx_pixel_at(buf, r.x, y), r.w, color);
}

void gfx_stroke_rect(gfx_buffer_t *buf, gfx_rect_t r, int32_t thickness, gfx_color_t color)
{
    if (gfx_rect_empty(r) || thickness <= 0)
        return;
    if (thickness * 2 >= r.w || thickness * 2 >= r.h)
    {
        gfx_fill_rect(buf, r, color);
        return;
    }
    // four parts that do not overlap, so a colour with alpha is even
    gfx_fill_rect(buf, gfx_rect(r.x, r.y, r.w, thickness), color);
    gfx_fill_rect(buf, gfx_rect(r.x, r.y + r.h - thickness, r.w, thickness), color);
    gfx_fill_rect(buf, gfx_rect(r.x, r.y + thickness, thickness, r.h - 2 * thickness), color);
    gfx_fill_rect(buf, gfx_rect(r.x + r.w - thickness, r.y + thickness, thickness,
                                r.h - 2 * thickness),
                  color);
}

void gfx_fill_gradient(gfx_buffer_t *buf, gfx_rect_t r, gfx_color_t top, gfx_color_t bottom)
{
    gfx_rect_t c = gfx_rect_intersect(r, gfx_clip_of(buf));
    for (int32_t y = c.y; y < c.y + c.h; y++)
    {
        uint32_t f = r.h > 1 ? (uint32_t)((y - r.y) * 256 / (r.h - 1)) : 0;
        uint32_t color = gfx_lerp_pixel(top, bottom, f);
        uint32_t *row = gfx_pixel_at(buf, c.x, y);
        for (int32_t i = 0; i < c.w; i++)
            row[i] = color;
    }
}

/* ---- copies --------------------------------------------------------------- */

/**
 * clip a copy from src_rect of src to (x, y) of dst.
 * @param out_x receives the first source pixel to use.
 * @return the destination rectangle (empty if nothing is left).
 */
static gfx_rect_t clip_copy(const gfx_buffer_t *dst, int32_t x, int32_t y,
                            const gfx_buffer_t *src, gfx_rect_t src_rect, int32_t *src_x,
                            int32_t *src_y)
{
    gfx_rect_t s = gfx_rect_intersect(src_rect, gfx_rect(0, 0, src->width, src->height));
    // where the clipped source lands
    gfx_rect_t d = gfx_rect(x + (s.x - src_rect.x), y + (s.y - src_rect.y), s.w, s.h);
    gfx_rect_t c = gfx_rect_intersect(d, gfx_clip_of(dst));
    *src_x = s.x + (c.x - d.x);
    *src_y = s.y + (c.y - d.y);
    return c;
}

void gfx_copy(gfx_buffer_t *dst, int32_t x, int32_t y, const gfx_buffer_t *src,
              gfx_rect_t src_rect)
{
    int32_t sx;
    int32_t sy;
    gfx_rect_t d = clip_copy(dst, x, y, src, src_rect, &sx, &sy);
    if (gfx_rect_empty(d))
        return;

    // rows from the bottom when a part moves down in the same pixels
    bool backwards = dst->pixels == src->pixels && d.y > sy;
    for (int32_t i = 0; i < d.h; i++)
    {
        int32_t row = backwards ? d.h - 1 - i : i;
        memmove(gfx_pixel_at(dst, d.x, d.y + row), gfx_pixel_at(src, sx, sy + row),
                (size_t)d.w * sizeof(uint32_t));
    }
}

void gfx_blend(gfx_buffer_t *dst, int32_t x, int32_t y, const gfx_buffer_t *src,
               gfx_rect_t src_rect, uint8_t opacity)
{
    int32_t sx;
    int32_t sy;
    gfx_rect_t d = clip_copy(dst, x, y, src, src_rect, &sx, &sy);
    if (gfx_rect_empty(d) || opacity == 0)
        return;

    for (int32_t row = 0; row < d.h; row++)
    {
        uint32_t *out = gfx_pixel_at(dst, d.x, d.y + row);
        const uint32_t *in = gfx_pixel_at(src, sx, sy + row);
        for (int32_t i = 0; i < d.w; i++)
        {
            uint32_t p = opacity == 255 ? in[i] : gfx_scale_pixel(in[i], opacity);
            out[i] = gfx_over_pixel(out[i], p);
        }
    }
}

void gfx_draw_mask(gfx_buffer_t *dst, int32_t x, int32_t y, const uint8_t *mask, int32_t width,
                   int32_t height, int32_t stride, gfx_color_t color)
{
    gfx_rect_t d = gfx_rect_intersect(gfx_rect(x, y, width, height), gfx_clip_of(dst));
    if (gfx_rect_empty(d) || (color >> 24) == 0)
        return;

    for (int32_t row = d.y; row < d.y + d.h; row++)
    {
        uint32_t *out = gfx_pixel_at(dst, d.x, row);
        const uint8_t *in = mask + (ptrdiff_t)(row - y) * stride + (d.x - x);
        for (int32_t i = 0; i < d.w; i++)
        {
            uint32_t coverage = in[i];
            if (coverage == 0)
                continue;
            uint32_t p = coverage == 255 ? color : gfx_scale_pixel(color, coverage);
            out[i] = gfx_over_pixel(out[i], p);
        }
    }
}

/* ---- output --------------------------------------------------------------- */

void gfx_present(void *fb, uint32_t fb_pitch, int32_t fb_width, int32_t fb_height,
                 const gfx_format_t *format, const gfx_buffer_t *src, gfx_rect_t src_rect)
{
    gfx_rect_t r = gfx_rect_intersect(src_rect, gfx_rect(0, 0, src->width, src->height));
    r = gfx_rect_intersect(r, gfx_rect(0, 0, fb_width, fb_height));
    if (gfx_rect_empty(r))
        return;

    uint32_t bpp = format->bytes_per_pixel;
    bool same = bpp == 4 && format->red_size == 8 && format->red_shift == 16 &&
                format->green_size == 8 && format->green_shift == 8 &&
                format->blue_size == 8 && format->blue_shift == 0;

    for (int32_t y = r.y; y < r.y + r.h; y++)
    {
        uint8_t *out = (uint8_t *)fb + (size_t)y * fb_pitch + (size_t)r.x * bpp;
        const uint32_t *in = gfx_pixel_at(src, r.x, y);
        if (same)
        {
            // the framebuffer ignores the top byte, so the row is copied as is
            memcpy(out, in, (size_t)r.w * 4);
            continue;
        }
        for (int32_t i = 0; i < r.w; i++)
        {
            uint32_t p = in[i];
            uint32_t value = ((((p >> 16) & 0xFF) >> (8 - format->red_size)) << format->red_shift) |
                             ((((p >> 8) & 0xFF) >> (8 - format->green_size))
                              << format->green_shift) |
                             (((p & 0xFF) >> (8 - format->blue_size)) << format->blue_shift);
            for (uint32_t b = 0; b < bpp; b++)
                out[b] = (uint8_t)(value >> (8 * b));
            out += bpp;
        }
    }
}
