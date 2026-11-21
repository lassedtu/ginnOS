#pragma once

/**
 * @file gfx_internal.h
 * @brief pixel arithmetic shared by the libgfx sources.
 *
 * the helpers work on two channels at once: red and blue in one 32-bit
 * word (mask 0x00FF00FF), alpha and green in another. each channel has
 * 16 bits of room, so a product of two bytes does not spill into the next.
 */

#include "gfx.h"

/**
 * every channel of p times a / 255, rounded exactly.
 * @param a 0 to 255.
 */
static inline uint32_t gfx_scale_pixel(uint32_t p, uint32_t a)
{
    uint32_t rb = (p & 0x00FF00FFu) * a + 0x00800080u;
    rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
    uint32_t ag = ((p >> 8) & 0x00FF00FFu) * a + 0x00800080u;
    ag = (ag + ((ag >> 8) & 0x00FF00FFu)) & 0xFF00FF00u;
    return rb | ag;
}

/**
 * src over dst, both premultiplied.
 */
static inline uint32_t gfx_over_pixel(uint32_t dst, uint32_t src)
{
    uint32_t a = src >> 24;
    if (a == 255)
        return src;
    if (a == 0)
        return dst;
    return src + gfx_scale_pixel(dst, 255 - a);
}

/**
 * a and b mixed: f / 256 of b and the rest of a.
 * @param f 0 to 256.
 */
static inline uint32_t gfx_lerp_pixel(uint32_t a, uint32_t b, uint32_t f)
{
    uint32_t inv = 256 - f;
    uint32_t rb = ((((a & 0x00FF00FFu) * inv) + ((b & 0x00FF00FFu) * f)) >> 8) & 0x00FF00FFu;
    uint32_t ag = ((((a >> 8) & 0x00FF00FFu) * inv) + (((b >> 8) & 0x00FF00FFu) * f)) &
                  0xFF00FF00u;
    return rb | ag;
}

/**
 * the buffer's clip rectangle, also clipped to the buffer.
 */
static inline gfx_rect_t gfx_clip_of(const gfx_buffer_t *buf)
{
    return gfx_rect_intersect(buf->clip, gfx_rect(0, 0, buf->width, buf->height));
}
