/**
 * @file gfx_shapes.c
 * @brief libgfx: lines and rectangles with round corners.
 */

#include "gfx_internal.h"

/**
 * how much of pixel (px, py) is inside the round rectangle r, 0 to 255.
 * a pixel in a corner square is sampled 4 x 4 times against the corner's
 * circle (positions in eighths of a pixel, so no fractions are needed).
 */
static uint32_t round_coverage(gfx_rect_t r, int32_t radius, int32_t px, int32_t py)
{
    int32_t cx;
    int32_t cy;
    if (px < r.x + radius)
        cx = r.x + radius;
    else if (px >= r.x + r.w - radius)
        cx = r.x + r.w - radius;
    else
        return 255;
    if (py < r.y + radius)
        cy = r.y + radius;
    else if (py >= r.y + r.h - radius)
        cy = r.y + r.h - radius;
    else
        return 255;

    int32_t limit = radius * 8 * radius * 8;
    uint32_t inside = 0;
    for (int32_t j = 0; j < 4; j++)
    {
        int32_t dy = py * 8 + 1 + 2 * j - cy * 8;
        for (int32_t i = 0; i < 4; i++)
        {
            int32_t dx = px * 8 + 1 + 2 * i - cx * 8;
            if (dx * dx + dy * dy <= limit)
                inside++;
        }
    }
    return inside * 255 / 16;
}

void gfx_fill_round_rect(gfx_buffer_t *buf, gfx_rect_t r, int32_t radius, gfx_color_t color)
{
    if (gfx_rect_empty(r))
        return;
    if (radius > r.w / 2)
        radius = r.w / 2;
    if (radius > r.h / 2)
        radius = r.h / 2;
    if (radius <= 0)
    {
        gfx_fill_rect(buf, r, color);
        return;
    }

    gfx_rect_t c = gfx_rect_intersect(r, gfx_clip_of(buf));
    for (int32_t y = c.y; y < c.y + c.h; y++)
    {
        bool corner_row = y < r.y + radius || y >= r.y + r.h - radius;
        uint32_t *row = gfx_pixel_at(buf, 0, y);
        for (int32_t x = c.x; x < c.x + c.w; x++)
        {
            uint32_t coverage = corner_row ? round_coverage(r, radius, x, y) : 255;
            if (coverage == 0)
                continue;
            uint32_t p = coverage == 255 ? color : gfx_scale_pixel(color, coverage);
            row[x] = gfx_over_pixel(row[x], p);
        }
    }
}

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
