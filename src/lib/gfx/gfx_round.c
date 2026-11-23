/**
 * @file gfx_round.c
 * @brief libgfx: round rectangles, borders and soft shadows, drawn as CSS
 *        draws them.
 *
 * every shape here is a rectangle on whole pixels with a corner radius. a
 * pixel's coverage comes from the distance d from its centre to the edge
 * of the shape (negative inside): 0.5 - d, cut to 0..1. this is the usual
 * anti-aliasing of browsers, so idunn looks close to its reference.
 *
 * shadows follow CSS box-shadow: the shape is moved by the offset, made
 * larger by the spread, blurred with sigma = blur / 2 (three box blurs,
 * as SVG and Chrome do it), and drawn only outside the box (inside it, for
 * an inset shadow).
 */

#include "gfx_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    double left;
    double top;
    double right;
    double bottom;
    double radius;
} shape_t;

/**
 * a round rectangle; the radius is made smaller if the rectangle is small
 * (as CSS does for one radius on all corners).
 */
static shape_t shape_of(gfx_rect_t r, double radius)
{
    double limit = (r.w < r.h ? r.w : r.h) / 2.0;
    if (radius > limit)
        radius = limit;
    if (radius < 0)
        radius = 0;
    return (shape_t){r.x, r.y, r.x + r.w, r.y + r.h, radius};
}

/**
 * the distance from (px, py) to the edge of the shape: below 0 inside.
 */
static double distance(const shape_t *s, double px, double py)
{
    double half_w = (s->right - s->left) / 2;
    double half_h = (s->bottom - s->top) / 2;
    double qx = fabs(px - (s->left + half_w)) - (half_w - s->radius);
    double qy = fabs(py - (s->top + half_h)) - (half_h - s->radius);
    double ox = qx > 0 ? qx : 0;
    double oy = qy > 0 ? qy : 0;
    double in = qx > qy ? qx : qy;
    return sqrt(ox * ox + oy * oy) + (in < 0 ? in : 0) - s->radius;
}

/**
 * how much of pixel (x, y) the shape covers, 0 to 255.
 */
static uint32_t coverage(const shape_t *s, int32_t x, int32_t y)
{
    double px = x + 0.5;
    double py = y + 0.5;
    if (px < s->left || px > s->right || py < s->top || py > s->bottom)
        return 0;
    // away from the corners every pixel centre is half a pixel or more in
    if ((px >= s->left + s->radius && px <= s->right - s->radius) ||
        (py >= s->top + s->radius && py <= s->bottom - s->radius))
        return 255;
    double c = 0.5 - distance(s, px, py);
    if (c <= 0)
        return 0;
    if (c >= 1)
        return 255;
    return (uint32_t)(c * 255 + 0.5);
}

/**
 * the shape's radius with a CSS spread: it grows with the spread, a little
 * less when the radius is smaller than the spread, and 0 stays 0.
 */
static double spread_radius(double radius, double spread)
{
    if (spread <= 0)
        return radius + spread > 0 ? radius + spread : 0;
    if (radius >= spread)
        return radius + spread;
    double t = radius / spread - 1;
    return radius + spread * (1 + t * t * t);
}

static void blend_covered(uint32_t *pixel, gfx_color_t color, uint32_t cover)
{
    if (cover == 0)
        return;
    uint32_t p = cover == 255 ? color : gfx_scale_pixel(color, cover);
    *pixel = gfx_over_pixel(*pixel, p);
}

/**
 * the colour of row y of a vertical gradient over r, sampled at the pixel
 * centre as CSS does.
 */
static gfx_color_t gradient_at(gfx_rect_t r, int32_t y, gfx_color_t top, gfx_color_t bottom)
{
    if (top == bottom || r.h <= 0)
        return top;
    uint32_t f = (uint32_t)(((int64_t)(y - r.y) * 2 + 1) * 256 / (2 * r.h));
    return gfx_lerp_pixel(top, bottom, f > 256 ? 256 : f);
}

void gfx_fill_round_rect_gradient(gfx_buffer_t *buf, gfx_rect_t r, int32_t radius,
                                  gfx_color_t top, gfx_color_t bottom)
{
    shape_t s = shape_of(r, radius);
    gfx_rect_t c = gfx_rect_intersect(r, gfx_clip_of(buf));
    for (int32_t y = c.y; y < c.y + c.h; y++)
    {
        gfx_color_t color = gradient_at(r, y, top, bottom);
        uint32_t *row = gfx_pixel_at(buf, 0, y);
        for (int32_t x = c.x; x < c.x + c.w; x++)
            blend_covered(&row[x], color, coverage(&s, x, y));
    }
}

void gfx_fill_round_rect(gfx_buffer_t *buf, gfx_rect_t r, int32_t radius, gfx_color_t color)
{
    gfx_fill_round_rect_gradient(buf, r, radius, color, color);
}

void gfx_border_round_rect(gfx_buffer_t *buf, gfx_rect_t r, int32_t radius, int32_t width,
                           gfx_color_t color)
{
    if (width <= 0 || gfx_rect_empty(r))
        return;
    shape_t outer = shape_of(r, radius);
    gfx_rect_t in = gfx_rect(r.x + width, r.y + width, r.w - 2 * width, r.h - 2 * width);
    shape_t inner = shape_of(in, outer.radius - width);
    bool hollow = !gfx_rect_empty(in);

    gfx_rect_t c = gfx_rect_intersect(r, gfx_clip_of(buf));
    // the middle of the box, away from the border and the corners, is empty
    int32_t margin = width > radius ? width : radius;
    for (int32_t y = c.y; y < c.y + c.h; y++)
    {
        uint32_t *row = gfx_pixel_at(buf, 0, y);
        bool middle_row = y >= r.y + margin && y < r.y + r.h - margin;
        for (int32_t x = c.x; x < c.x + c.w; x++)
        {
            if (middle_row && x >= r.x + margin && x < r.x + r.w - margin)
            {
                x = r.x + r.w - margin - 1; // jump to the right side
                continue;
            }
            uint32_t a = coverage(&outer, x, y);
            uint32_t b = hollow ? coverage(&inner, x, y) : 0;
            blend_covered(&row[x], color, a > b ? a - b : 0);
        }
    }
}

/**
 * one box blur of a row or a column of n values, step apart: each value
 * becomes the mean of the size values from start to start + size - 1
 * around it (outside the line counts as 0).
 */
static void box_blur(uint8_t *line, int32_t n, int32_t step, int32_t size, int32_t before,
                     uint8_t *tmp)
{
    for (int32_t i = 0; i < n; i++)
        tmp[i] = line[(ptrdiff_t)i * step];
    int32_t sum = 0;
    // the window for output i is [i - before, i - before + size)
    for (int32_t k = -before; k < -before + size; k++)
        sum += k >= 0 && k < n ? tmp[k] : 0;
    for (int32_t i = 0; i < n; i++)
    {
        line[(ptrdiff_t)i * step] = (uint8_t)((sum + size / 2) / size);
        int32_t out = i - before;
        int32_t in = i - before + size;
        sum -= out >= 0 && out < n ? tmp[out] : 0;
        sum += in >= 0 && in < n ? tmp[in] : 0;
    }
}

/**
 * blur a mask in both directions like a Gaussian with this sigma: three box
 * blurs (the SVG feGaussianBlur method).
 */
static void blur_mask(uint8_t *mask, int32_t width, int32_t height, double sigma)
{
    int32_t d = (int32_t)floor(sigma * 3 * sqrt(2 * M_PI) / 4 + 0.5);
    if (d < 2)
        return;
    // an odd size is centred; an even one is used twice, once on each side
    // of the pixel, then once one larger and centred
    int32_t sizes[3] = {d, d, d};
    int32_t befores[3] = {d / 2, d / 2, d / 2};
    if (d % 2 == 0)
    {
        befores[0] = d / 2;
        befores[1] = d / 2 - 1;
        sizes[2] = d + 1;
        befores[2] = d / 2;
    }
    int32_t longest = width > height ? width : height;
    uint8_t *tmp = malloc((size_t)longest);
    if (!tmp)
        return;
    for (int32_t pass = 0; pass < 3; pass++)
    {
        for (int32_t y = 0; y < height; y++)
            box_blur(mask + (ptrdiff_t)y * width, width, 1, sizes[pass], befores[pass], tmp);
        for (int32_t x = 0; x < width; x++)
            box_blur(mask + x, height, width, sizes[pass], befores[pass], tmp);
    }
    free(tmp);
}

/** what a shadow is made of: its shape, the box it is cut by, the blur */
typedef struct
{
    shape_t clip;   // the box: an outer shadow shows outside it, an inset one in it
    shape_t shadow; // the shadow's shape
    bool shadow_empty;
    double sigma;
    int32_t pad;
} shadow_plan_t;

/** draw the shadow in one part of the buffer */
static void shadow_part(gfx_buffer_t *buf, const shadow_plan_t *p, const gfx_shadow_t *s,
                        gfx_rect_t area)
{
    if (gfx_rect_empty(area))
        return;
    // the mask, with room for the blur around the part that is drawn
    int32_t pad = p->pad;
    gfx_rect_t m = gfx_rect(area.x - pad, area.y - pad, area.w + 2 * pad, area.h + 2 * pad);
    uint8_t *mask = malloc((size_t)m.w * (size_t)m.h);
    if (!mask)
        return;
    for (int32_t y = 0; y < m.h; y++)
    {
        for (int32_t x = 0; x < m.w; x++)
        {
            uint32_t in = p->shadow_empty ? 0 : coverage(&p->shadow, m.x + x, m.y + y);
            // an inset shadow is everything outside its shape
            mask[(ptrdiff_t)y * m.w + x] = (uint8_t)(s->inset ? 255 - in : in);
        }
    }
    if (s->blur > 0)
        blur_mask(mask, m.w, m.h, p->sigma);

    for (int32_t y = area.y; y < area.y + area.h; y++)
    {
        uint32_t *row = gfx_pixel_at(buf, 0, y);
        const uint8_t *line = mask + (ptrdiff_t)(y - m.y) * m.w - m.x;
        for (int32_t x = area.x; x < area.x + area.w; x++)
        {
            // outer shadows show only outside the box, inset ones only in it
            uint32_t box_cover = coverage(&p->clip, x, y);
            uint32_t keep = s->inset ? box_cover : 255 - box_cover;
            blend_covered(&row[x], s->color, line[x] * keep / 255);
        }
    }
    free(mask);
}

void gfx_box_shadow(gfx_buffer_t *buf, gfx_rect_t box, int32_t radius, const gfx_shadow_t *s)
{
    if (gfx_rect_empty(box) || (s->color >> 24) == 0)
        return;
    shadow_plan_t p;
    p.clip = shape_of(box, radius);
    p.sigma = s->blur / 2.0;
    p.pad = s->blur > 0 ? (int32_t)ceil(p.sigma * 3) + 1 : 0;

    // the shadow's shape: the box moved, then grown (outer) or shrunk
    // (inset) by the spread
    int32_t grow = s->inset ? -s->spread : s->spread;
    gfx_rect_t moved = gfx_rect(box.x + s->x - grow, box.y + s->y - grow, box.w + 2 * grow,
                                box.h + 2 * grow);
    p.shadow = shape_of(moved, spread_radius(p.clip.radius, grow));
    p.shadow_empty = moved.w <= 0 || moved.h <= 0;

    // where it can show: around the shadow (outer), or the box (inset)
    gfx_rect_t area = s->inset ? box
                               : gfx_rect(moved.x - p.pad, moved.y - p.pad, moved.w + 2 * p.pad,
                                          moved.h + 2 * p.pad);
    area = gfx_rect_intersect(area, gfx_clip_of(buf));
    if (gfx_rect_empty(area))
        return;
    if (s->inset)
    {
        shadow_part(buf, &p, s, area);
        return;
    }

    // an outer shadow does not show in the middle of the box, which the box
    // covers whole: only the bands around it are made (a part of a large
    // card that is drawn again then costs nothing)
    int32_t r = p.clip.radius > 0 ? (int32_t)ceil(p.clip.radius) + 1 : 1;
    gfx_rect_t inner = gfx_rect(box.x + r, box.y + r, box.w - 2 * r, box.h - 2 * r);
    inner = gfx_rect_intersect(inner, area);
    if (gfx_rect_empty(inner))
    {
        shadow_part(buf, &p, s, area);
        return;
    }
    int32_t top = inner.y - area.y;
    int32_t bottom = area.y + area.h - (inner.y + inner.h);
    shadow_part(buf, &p, s, gfx_rect(area.x, area.y, area.w, top));
    shadow_part(buf, &p, s, gfx_rect(area.x, inner.y + inner.h, area.w, bottom));
    shadow_part(buf, &p, s, gfx_rect(area.x, inner.y, inner.x - area.x, inner.h));
    shadow_part(buf, &p, s, gfx_rect(inner.x + inner.w, inner.y,
                                     area.x + area.w - (inner.x + inner.w), inner.h));
}
