/**
 * @file gfx_shapes.c
 * @brief libgfx: lines.
 */

#include "gfx_internal.h"

void gfx_line(gfx_buffer_t *buf, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
              gfx_color_t color)
{
    // Bresenham: one pixel per step along the longer axis
    gfx_rect_t c = gfx_clip_of(buf);
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0; // negative
    int32_t step_x = x0 < x1 ? 1 : -1;
    int32_t step_y = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    for (;;)
    {
        if (gfx_rect_contains(c, x0, y0))
        {
            uint32_t *p = gfx_pixel_at(buf, x0, y0);
            *p = gfx_over_pixel(*p, color);
        }
        if (x0 == x1 && y0 == y1)
            break;
        int32_t e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += step_x;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += step_y;
        }
    }
}
