#pragma once

/**
 * @file picture.h
 * @brief Freyja: a picture and its smaller copies, drawn at any zoom.
 *
 * only gfx (no idunn), so the host tests run it. a smaller copy is half
 * the size of the one before it, each pixel the mean of four: drawing a
 * large photo small starts from the copy nearest its size, so it does
 * not shimmer. above 100 % the pixels are drawn as squares (no blur).
 */

#include <gfx.h>
#include <stdbool.h>

#define LEVELS_MAX 12

typedef struct
{
    gfx_buffer_t *levels[LEVELS_MAX]; // [0] is the picture itself
    int level_count;
} picture_t;

/** take a decoded picture (picture_t owns it now); false without memory */
bool picture_set(picture_t *p, gfx_buffer_t *image);
void picture_free(picture_t *p);

static inline int32_t picture_width(const picture_t *p)
{
    return p->level_count ? p->levels[0]->width : 0;
}

static inline int32_t picture_height(const picture_t *p)
{
    return p->level_count ? p->levels[0]->height : 0;
}

/** a quarter turn to the right (left when left is true) */
bool picture_rotate(picture_t *p, bool left);

/** a buffer half as large, each pixel the mean of four (premultiplied) */
gfx_buffer_t *picture_halve(const gfx_buffer_t *src);

/**
 * draw the picture at zoom, its top left corner at (x, y) in dst (which
 * may be outside), over a checked pattern where it is see-through. only
 * dst's clip is drawn.
 */
void picture_draw(const picture_t *p, gfx_buffer_t *dst, double x, double y, double zoom,
                  gfx_color_t check_a, gfx_color_t check_b, int32_t check_size);
