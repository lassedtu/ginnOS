/**
 * @file picture.c
 * @brief Freyja: a picture and its smaller copies, drawn at any zoom.
 */

#include "picture.h"

#include <stdint.h>
#include <stdlib.h>

/** src over dst, premultiplied */
static uint32_t over(uint32_t src, uint32_t dst)
{
    uint32_t a = src >> 24;
    if (a == 255)
        return src;
    if (a == 0)
        return dst;
    uint32_t keep = 255 - a;
    uint32_t rb = ((dst & 0x00FF00FFu) * keep + 0x00800080u) >> 8 & 0x00FF00FFu;
    uint32_t g = ((dst & 0x0000FF00u) * keep + 0x00008000u) >> 8 & 0x0000FF00u;
    uint32_t alpha = a + (((dst >> 24) * keep + 128) >> 8);
    return (alpha > 255 ? 255 : alpha) << 24 | ((src & 0x00FFFFFFu) + rb + g);
}

gfx_buffer_t *picture_halve(const gfx_buffer_t *src)
{
    int32_t w = src->width / 2 > 0 ? src->width / 2 : 1;
    int32_t h = src->height / 2 > 0 ? src->height / 2 : 1;
    gfx_buffer_t *out = gfx_buffer_new(w, h);
    if (!out)
        return NULL;
    for (int32_t y = 0; y < h; y++)
    {
        int32_t y0 = y * 2 < src->height ? y * 2 : src->height - 1;
        int32_t y1 = y0 + 1 < src->height ? y0 + 1 : y0;
        const uint32_t *r0 = src->pixels + (size_t)y0 * src->stride;
        const uint32_t *r1 = src->pixels + (size_t)y1 * src->stride;
        uint32_t *o = out->pixels + (size_t)y * out->stride;
        for (int32_t x = 0; x < w; x++)
        {
            int32_t x0 = x * 2 < src->width ? x * 2 : src->width - 1;
            int32_t x1 = x0 + 1 < src->width ? x0 + 1 : x0;
            uint32_t q[4] = {r0[x0], r0[x1], r1[x0], r1[x1]};
            uint32_t sum[4] = {0, 0, 0, 0};
            for (int i = 0; i < 4; i++)
                for (int c = 0; c < 4; c++)
                    sum[c] += q[i] >> (c * 8) & 0xFF;
            o[x] = (sum[3] + 2) / 4 << 24 | (sum[2] + 2) / 4 << 16 | (sum[1] + 2) / 4 << 8 |
                   (sum[0] + 2) / 4;
        }
    }
    return out;
}

static void free_levels(picture_t *p, int from)
{
    for (int i = from; i < p->level_count; i++)
        gfx_buffer_free(p->levels[i]);
    p->level_count = from;
}

/** the smaller copies of levels[0], down to one of 64 pixels or less */
static void make_levels(picture_t *p)
{
    free_levels(p, 1);
    while (p->level_count < LEVELS_MAX)
    {
        const gfx_buffer_t *last = p->levels[p->level_count - 1];
        if (last->width <= 64 && last->height <= 64)
            break;
        gfx_buffer_t *half = picture_halve(last);
        if (!half)
            break;
        p->levels[p->level_count++] = half;
    }
}

bool picture_set(picture_t *p, gfx_buffer_t *image)
{
    picture_free(p);
    if (!image)
        return false;
    p->levels[0] = image;
    p->level_count = 1;
    make_levels(p);
    return true;
}

void picture_free(picture_t *p)
{
    free_levels(p, 0);
}

bool picture_rotate(picture_t *p, bool left)
{
    if (!p->level_count)
        return false;
    const gfx_buffer_t *src = p->levels[0];
    gfx_buffer_t *out = gfx_buffer_new(src->height, src->width);
    if (!out)
        return false;
    for (int32_t y = 0; y < src->height; y++)
    {
        const uint32_t *row = src->pixels + (size_t)y * src->stride;
        for (int32_t x = 0; x < src->width; x++)
        {
            // right: (x, y) goes to (h - 1 - y, x); left: to (y, w - 1 - x)
            int32_t ox = left ? y : src->height - 1 - y;
            int32_t oy = left ? src->width - 1 - x : x;
            out->pixels[(size_t)oy * out->stride + ox] = row[x];
        }
    }
    free_levels(p, 0);
    p->levels[0] = out;
    p->level_count = 1;
    make_levels(p);
    return true;
}

static uint32_t check(int32_t x, int32_t y, int32_t size, gfx_color_t a, gfx_color_t b)
{
    return ((x / size + y / size) & 1) ? b : a;
}

void picture_draw(const picture_t *p, gfx_buffer_t *dst, double x, double y, double zoom,
                  gfx_color_t check_a, gfx_color_t check_b, int32_t check_size)
{
    if (!p->level_count || zoom <= 0)
        return;
    // the copy whose size is nearest above what is drawn
    int level = 0;
    double scale = zoom;
    while (level + 1 < p->level_count && scale <= 0.5)
    {
        level++;
        scale *= 2;
    }
    const gfx_buffer_t *src = p->levels[level];
    // its size on the screen (from the full picture, so levels line up)
    double w = picture_width(p) * zoom;
    double h = picture_height(p) * zoom;
    gfx_rect_t area = gfx_rect((int32_t)x, (int32_t)y, (int32_t)(w + 0.5), (int32_t)(h + 0.5));
    gfx_rect_t c = gfx_rect_intersect(area, dst->clip);
    c = gfx_rect_intersect(c, gfx_rect(0, 0, dst->width, dst->height));
    if (gfx_rect_empty(c))
        return;
    double sx = src->width / w;  // source pixels per screen pixel
    double sy = src->height / h;
    bool sharp = zoom >= 1.0;
    for (int32_t py = c.y; py < c.y + c.h; py++)
    {
        uint32_t *out = dst->pixels + (size_t)py * dst->stride;
        double fy = (py - y + 0.5) * sy - (sharp ? 0 : 0.5);
        for (int32_t px = c.x; px < c.x + c.w; px++)
        {
            double fx = (px - x + 0.5) * sx - (sharp ? 0 : 0.5);
            uint32_t color;
            if (sharp)
            {
                int32_t ix = (int32_t)fx;
                int32_t iy = (int32_t)fy;
                ix = ix < 0 ? 0 : ix >= src->width ? src->width - 1 : ix;
                iy = iy < 0 ? 0 : iy >= src->height ? src->height - 1 : iy;
                color = src->pixels[(size_t)iy * src->stride + ix];
            }
            else
            {
                // bilinear, between the four nearest pixels
                if (fx < 0)
                    fx = 0;
                if (fy < 0)
                    fy = 0;
                int32_t x0 = (int32_t)fx;
                int32_t y0 = (int32_t)fy;
                int32_t x1 = x0 + 1 < src->width ? x0 + 1 : src->width - 1;
                int32_t y1 = y0 + 1 < src->height ? y0 + 1 : src->height - 1;
                if (x0 >= src->width)
                    x0 = x1 = src->width - 1;
                if (y0 >= src->height)
                    y0 = y1 = src->height - 1;
                uint32_t tx = (uint32_t)((fx - x0) * 256);
                uint32_t ty = (uint32_t)((fy - y0) * 256);
                const uint32_t *r0 = src->pixels + (size_t)y0 * src->stride;
                const uint32_t *r1 = src->pixels + (size_t)y1 * src->stride;
                color = 0;
                for (int ch = 0; ch < 4; ch++)
                {
                    int s = ch * 8;
                    uint32_t a = (r0[x0] >> s & 0xFF) * (256 - tx) + (r0[x1] >> s & 0xFF) * tx;
                    uint32_t b = (r1[x0] >> s & 0xFF) * (256 - tx) + (r1[x1] >> s & 0xFF) * tx;
                    uint32_t v = (a * (256 - ty) + b * ty + 32768) >> 16;
                    color |= (v > 255 ? 255 : v) << s;
                }
            }
            out[px] = over(color, check(px - area.x, py - area.y, check_size, check_a, check_b));
        }
    }
}
