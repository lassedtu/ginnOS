#pragma once

/**
 * @file gfx.h
 * @brief libgfx: drawing into pixel buffers in memory.
 *
 * every buffer has one pixel format: 32 bits per pixel, premultiplied
 * ARGB (0xAARRGGBB in a uint32_t, each colour already multiplied by the
 * alpha). premultiplied pixels make blending one multiply per channel,
 * and scaling and blending give no dark edges. only gfx_present changes
 * the format, when it copies a buffer to the framebuffer.
 *
 * coordinates are pixels, x to the right and y down, with (0, 0) at the
 * top left. a rectangle includes its left and top edges and excludes its
 * right and bottom edges. every drawing call is clipped to the buffer's
 * clip rectangle, so drawing outside the buffer is safe and does nothing.
 *
 * the drawing is integer only, so the host and ginnOS make the same
 * pixels (the host tests compare golden images).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * a premultiplied ARGB colour, 0xAARRGGBB. make one with gfx_rgb or
 * gfx_rgba.
 */
typedef uint32_t gfx_color_t;

typedef struct
{
    int32_t x;
    int32_t y;
    int32_t w; // 0 or less: the rectangle is empty
    int32_t h;
} gfx_rect_t;

typedef struct
{
    uint32_t *pixels;
    int32_t width;
    int32_t height;
    int32_t stride;  // pixels from one row to the next (at least width)
    gfx_rect_t clip; // drawing stays inside this (and inside the buffer)
    bool owned;      // gfx_buffer_free frees the pixels
} gfx_buffer_t;

/**
 * the pixel format of a framebuffer, for gfx_present. the sizes and
 * shifts are those of the framebuffer's FBIOGET_VSCREENINFO.
 */
typedef struct
{
    uint8_t bytes_per_pixel; // 2, 3 or 4
    uint8_t red_size;
    uint8_t red_shift;
    uint8_t green_size;
    uint8_t green_shift;
    uint8_t blue_size;
    uint8_t blue_shift;
} gfx_format_t;

/* ---- colours -------------------------------------------------------------- */

#define GFX_TRANSPARENT ((gfx_color_t)0)

static inline gfx_color_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/**
 * a colour with alpha, from straight (not premultiplied) channels.
 */
gfx_color_t gfx_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

static inline uint8_t gfx_alpha(gfx_color_t c)
{
    return (uint8_t)(c >> 24);
}

/**
 * a colour with its alpha (and so its premultiplied channels) scaled by
 * opacity / 255.
 */
gfx_color_t gfx_fade(gfx_color_t c, uint8_t opacity);

/**
 * a source pixel over a destination pixel (Porter-Duff source over).
 */
gfx_color_t gfx_over(gfx_color_t dst, gfx_color_t src);

/* ---- rectangles ----------------------------------------------------------- */

static inline gfx_rect_t gfx_rect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    gfx_rect_t r = {x, y, w, h};
    return r;
}

static inline bool gfx_rect_empty(gfx_rect_t r)
{
    return r.w <= 0 || r.h <= 0;
}

/**
 * the part two rectangles share (empty if none).
 */
gfx_rect_t gfx_rect_intersect(gfx_rect_t a, gfx_rect_t b);

/**
 * the smallest rectangle that holds both (an empty one counts as none).
 */
gfx_rect_t gfx_rect_union(gfx_rect_t a, gfx_rect_t b);

bool gfx_rect_contains(gfx_rect_t r, int32_t x, int32_t y);

/* ---- buffers -------------------------------------------------------------- */

/**
 * a new buffer of transparent pixels, from malloc.
 * @return the buffer, or NULL (size 0 or out of memory).
 */
gfx_buffer_t *gfx_buffer_new(int32_t width, int32_t height);

/**
 * free a buffer from gfx_buffer_new (and its pixels, if it owns them).
 */
void gfx_buffer_free(gfx_buffer_t *buf);

/**
 * make a buffer over memory the caller owns (a shared memory mapping, for
 * example). the clip rectangle is the whole buffer.
 * @param stride pixels from one row to the next.
 */
void gfx_buffer_init(gfx_buffer_t *buf, uint32_t *pixels, int32_t width, int32_t height,
                     int32_t stride);

/**
 * a buffer for a part of another buffer: the same pixels, its own clip.
 * the rectangle is clipped to the parent. it is not freed on its own.
 */
gfx_buffer_t gfx_buffer_sub(const gfx_buffer_t *parent, gfx_rect_t r);

/**
 * set the clip rectangle (it is clipped to the buffer).
 */
void gfx_set_clip(gfx_buffer_t *buf, gfx_rect_t clip);

/**
 * make the whole buffer the clip rectangle.
 */
void gfx_reset_clip(gfx_buffer_t *buf);

static inline uint32_t *gfx_pixel_at(const gfx_buffer_t *buf, int32_t x, int32_t y)
{
    return buf->pixels + (ptrdiff_t)y * buf->stride + x;
}

/* ---- drawing -------------------------------------------------------------- */

/**
 * set every pixel in the clip rectangle to a colour (no blending).
 */
void gfx_clear(gfx_buffer_t *buf, gfx_color_t color);

/**
 * fill a rectangle, blended over what is there.
 */
void gfx_fill_rect(gfx_buffer_t *buf, gfx_rect_t r, gfx_color_t color);

/**
 * the edges of a rectangle, thickness pixels wide, on the inside.
 */
void gfx_stroke_rect(gfx_buffer_t *buf, gfx_rect_t r, int32_t thickness, gfx_color_t color);

/**
 * fill a rectangle with round corners, with anti-aliased corners (as CSS
 * border-radius).
 * @param radius the corner radius (made smaller if the rectangle is small).
 */
void gfx_fill_round_rect(gfx_buffer_t *buf, gfx_rect_t r, int32_t radius, gfx_color_t color);

/**
 * fill a round rectangle with a vertical gradient from top to bottom,
 * sampled at the pixel centres (as a CSS linear-gradient).
 */
void gfx_fill_round_rect_gradient(gfx_buffer_t *buf, gfx_rect_t r, int32_t radius,
                                  gfx_color_t top, gfx_color_t bottom);

/**
 * the border of a round rectangle: width pixels wide, inside r (as a CSS
 * border with box-sizing: border-box). the inside is not touched.
 */
void gfx_border_round_rect(gfx_buffer_t *buf, gfx_rect_t r, int32_t radius, int32_t width,
                           gfx_color_t color);

/**
 * a CSS box-shadow: "x y blur spread color", and "inset".
 */
typedef struct
{
    int32_t x;      // the offset
    int32_t y;
    int32_t blur;   // the CSS blur radius: a Gaussian blur with sigma = blur / 2
    int32_t spread; // grows the shape (shrinks it for an inset shadow)
    gfx_color_t color;
    bool inset;     // inside the box instead of outside it
} gfx_shadow_t;

/**
 * draw a box-shadow of a round box. an outer shadow is drawn only outside
 * the box and an inset one only inside it, as in CSS: draw outer shadows
 * before the box, inset ones after its background.
 * @param box the border box (an inset shadow: the box inside the border).
 */
void gfx_box_shadow(gfx_buffer_t *buf, gfx_rect_t box, int32_t radius, const gfx_shadow_t *shadow);

/**
 * a line one pixel wide from (x0, y0) to (x1, y1), both ends included,
 * blended over what is there.
 */
void gfx_line(gfx_buffer_t *buf, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
              gfx_color_t color);

/**
 * copy a part of src to dst at (x, y), replacing the pixels (no blending).
 * the buffers may be the same one, and the parts may overlap.
 */
void gfx_copy(gfx_buffer_t *dst, int32_t x, int32_t y, const gfx_buffer_t *src,
              gfx_rect_t src_rect);

/**
 * blend a part of src over dst at (x, y), with an opacity (255: as it is).
 */
void gfx_blend(gfx_buffer_t *dst, int32_t x, int32_t y, const gfx_buffer_t *src,
               gfx_rect_t src_rect, uint8_t opacity);

/**
 * draw a part of src scaled to fill dst_rect, blended over dst, with
 * bilinear filtering.
 */
void gfx_blend_scaled(gfx_buffer_t *dst, gfx_rect_t dst_rect, const gfx_buffer_t *src,
                      gfx_rect_t src_rect, uint8_t opacity);

/**
 * a coverage mask (one byte per pixel, 0 to 255) in a colour, blended
 * over dst with its top left at (x, y). text and line icons are drawn
 * this way.
 * @param stride bytes from one mask row to the next.
 */
void gfx_draw_mask(gfx_buffer_t *dst, int32_t x, int32_t y, const uint8_t *mask, int32_t width,
                   int32_t height, int32_t stride, gfx_color_t color);

/**
 * a vertical gradient from top to bottom colour over a rectangle
 * (replacing the pixels). for backgrounds.
 */
void gfx_fill_gradient(gfx_buffer_t *buf, gfx_rect_t r, gfx_color_t top, gfx_color_t bottom);

/* ---- output --------------------------------------------------------------- */

/**
 * copy a part of a buffer to a framebuffer, in its pixel format. the
 * pixels are taken as over black (alpha is dropped).
 * @param fb the framebuffer's first pixel.
 * @param fb_pitch bytes from one framebuffer row to the next.
 * @param fb_width the framebuffer's size, to clip to.
 * @param src_rect the part of src; it goes to the same place on the
 *        framebuffer.
 */
void gfx_present(void *fb, uint32_t fb_pitch, int32_t fb_width, int32_t fb_height,
                 const gfx_format_t *format, const gfx_buffer_t *src, gfx_rect_t src_rect);
