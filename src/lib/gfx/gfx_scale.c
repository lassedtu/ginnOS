/**
 * @file gfx_scale.c
 * @brief libgfx: drawing a buffer at another size (bilinear filtering).
 *
 * the source position of each destination pixel is kept in 16.16 fixed
 * point: the centre of destination pixel i maps to the source position
 * (i + 0.5) * scale - 0.5. a sample between pixels mixes the four
 * neighbours; at the edges the nearest pixel is used again.
 */

#include "gfx_internal.h"

/**
 * the 16.16 source position of the first destination pixel, and the step.
 */
static void setup_axis(int32_t src_start, int32_t src_size, int32_t dst_size, int32_t *first,
                       int32_t *step)
{
    *step = (int32_t)(((int64_t)src_size << 16) / dst_size);
    *first = (src_start << 16) + *step / 2 - 32768;
}

/**
 * split a 16.16 position into the pixel before it and the weight of the
 * pixel after it (0 to 256), kept inside [lo, hi].
 */
static void sample_axis(int32_t pos, int32_t lo, int32_t hi, int32_t *p0, int32_t *p1,
                        uint32_t *weight)
{
    if (pos < lo << 16)
    {
        *p0 = *p1 = lo;
        *weight = 0;
        return;
    }
    *p0 = pos >> 16;
    *weight = (uint32_t)((pos >> 8) & 0xFF);
    if (*p0 >= hi)
    {
        *p0 = *p1 = hi;
        *weight = 0;
        return;
    }
    *p1 = *p0 + 1;
}

void gfx_blend_scaled(gfx_buffer_t *dst, gfx_rect_t dst_rect, const gfx_buffer_t *src,
                      gfx_rect_t src_rect, uint8_t opacity)
{
    src_rect = gfx_rect_intersect(src_rect, gfx_rect(0, 0, src->width, src->height));
    if (gfx_rect_empty(src_rect) || gfx_rect_empty(dst_rect) || opacity == 0)
        return;
    gfx_rect_t c = gfx_rect_intersect(dst_rect, gfx_clip_of(dst));
    if (gfx_rect_empty(c))
        return;

    int32_t first_x;
    int32_t step_x;
    int32_t first_y;
    int32_t step_y;
    setup_axis(src_rect.x, src_rect.w, dst_rect.w, &first_x, &step_x);
    setup_axis(src_rect.y, src_rect.h, dst_rect.h, &first_y, &step_y);
    int32_t last_x = src_rect.x + src_rect.w - 1;
    int32_t last_y = src_rect.y + src_rect.h - 1;

    for (int32_t y = c.y; y < c.y + c.h; y++)
    {
        int32_t sy0;
        int32_t sy1;
        uint32_t wy;
        sample_axis(first_y + (y - dst_rect.y) * step_y, src_rect.y, last_y, &sy0, &sy1, &wy);
        const uint32_t *row0 = gfx_pixel_at(src, 0, sy0);
        const uint32_t *row1 = gfx_pixel_at(src, 0, sy1);
        uint32_t *out = gfx_pixel_at(dst, 0, y);

        for (int32_t x = c.x; x < c.x + c.w; x++)
        {
            int32_t sx0;
            int32_t sx1;
            uint32_t wx;
            sample_axis(first_x + (x - dst_rect.x) * step_x, src_rect.x, last_x, &sx0, &sx1,
                        &wx);
            uint32_t top = gfx_lerp_pixel(row0[sx0], row0[sx1], wx);
            uint32_t bottom = gfx_lerp_pixel(row1[sx0], row1[sx1], wx);
            uint32_t p = gfx_lerp_pixel(top, bottom, wy);
            if (opacity != 255)
                p = gfx_scale_pixel(p, opacity);
            out[x] = gfx_over_pixel(out[x], p);
        }
    }
}
