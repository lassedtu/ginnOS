#pragma once

/**
 * @file bragi_internal.h
 * @brief what the bragi sources share: the font, the face and the glyph.
 */

#include "bragi.h"

#define STBTT_assert(x) ((void)0)
#include "../stb/stb_truetype.h"

// positions along a line are kept in 1/64 pixels
#define BRAGI_UNITS 64

// glyphs are made at 4 horizontal positions: 0, 0.25, 0.5 and 0.75 px
#define BRAGI_SUBPIXELS 4

#define BRAGI_HASH_SIZE 256

struct bragi_font
{
    stbtt_fontinfo info;
    uint8_t *data;
    bool owns_data;
};

/**
 * one glyph image in a face's cache.
 */
typedef struct bragi_glyph
{
    uint32_t key;      // glyph index * BRAGI_SUBPIXELS + subpixel position
    int16_t left;      // from the pen to the mask's left edge
    int16_t top;       // from the baseline to the mask's top edge (negative: above)
    uint16_t width;
    uint16_t height;
    uint8_t *mask;     // width * height coverage bytes (NULL when empty)
    struct bragi_glyph *hash_next;
    struct bragi_glyph *newer; // the cache's use order (newest first)
    struct bragi_glyph *older;
} bragi_glyph_t;

struct bragi_face
{
    bragi_font_t *font;
    bragi_face_t *fallback;
    float scale;           // font units to pixels
    int32_t ascent;        // pixels
    int32_t descent;
    int32_t line_height;

    bragi_glyph_t *hash[BRAGI_HASH_SIZE];
    bragi_glyph_t *newest;
    bragi_glyph_t *oldest;
    uint32_t budget;       // bytes the cached masks may use
    bragi_stats_t stats;
};

/**
 * find the face (this one or a fallback) and the glyph for a character.
 * with no face that has it, the face itself and glyph 0 (the "missing
 * glyph" box).
 */
bragi_face_t *bragi_find_glyph(bragi_face_t *face, uint32_t codepoint, int *glyph);

/**
 * the advance of a glyph in 1/64 pixels.
 */
int32_t bragi_advance(const bragi_face_t *face, int glyph);

/**
 * the kerning between two glyphs of the same face, in 1/64 pixels.
 */
int32_t bragi_kerning(const bragi_face_t *face, int left, int right);

/**
 * a glyph image from the cache, made if it is not there.
 * @param subpixel 0 to BRAGI_SUBPIXELS - 1.
 * @return the glyph, or NULL when out of memory.
 */
const bragi_glyph_t *bragi_glyph(bragi_face_t *face, int glyph, int subpixel);

/**
 * blend a coverage mask in a colour over buf, in linear light.
 * @param color premultiplied; its alpha scales the coverage.
 */
void bragi_blend_mask(gfx_buffer_t *buf, int32_t x, int32_t y, const uint8_t *mask,
                      int32_t width, int32_t height, int32_t stride, gfx_color_t color);
