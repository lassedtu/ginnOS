/**
 * @file bragi_cache.c
 * @brief bragi: the glyph cache of a face, and blending glyph masks.
 *
 * each face keeps the glyph images it made, by glyph and subpixel
 * position, in a hash table. a list in use order lets the cache drop the
 * glyph used least recently when the masks use more than the budget.
 */

#include "bragi_internal.h"

#include <stdlib.h>
#include <string.h>

static uint32_t glyph_cost(const bragi_glyph_t *g)
{
    return (uint32_t)sizeof(*g) + (uint32_t)g->width * g->height;
}

static uint32_t hash_of(uint32_t key)
{
    return (key * 2654435761u) >> 24; // BRAGI_HASH_SIZE is 256
}

static void unlink_use(bragi_face_t *face, bragi_glyph_t *g)
{
    if (g->newer)
        g->newer->older = g->older;
    else
        face->newest = g->older;
    if (g->older)
        g->older->newer = g->newer;
    else
        face->oldest = g->newer;
    g->newer = NULL;
    g->older = NULL;
}

static void link_newest(bragi_face_t *face, bragi_glyph_t *g)
{
    g->older = face->newest;
    g->newer = NULL;
    if (face->newest)
        face->newest->newer = g;
    face->newest = g;
    if (!face->oldest)
        face->oldest = g;
}

static void drop(bragi_face_t *face, bragi_glyph_t *g)
{
    bragi_glyph_t **link = &face->hash[hash_of(g->key)];
    while (*link && *link != g)
        link = &(*link)->hash_next;
    if (*link)
        *link = g->hash_next;
    unlink_use(face, g);
    face->stats.bytes -= glyph_cost(g);
    face->stats.evicted++;
    free(g->mask);
    free(g);
}

/**
 * make the image of a glyph at a subpixel position.
 */
static bragi_glyph_t *make_glyph(bragi_face_t *face, int glyph, int subpixel, uint32_t key)
{
    const stbtt_fontinfo *info = &face->font->info;
    float shift = (float)subpixel / BRAGI_SUBPIXELS;
    int x0;
    int y0;
    int x1;
    int y1;
    stbtt_GetGlyphBitmapBoxSubpixel(info, glyph, face->scale, face->scale, shift, 0.0f, &x0, &y0,
                                    &x1, &y1);

    bragi_glyph_t *g = calloc(1, sizeof(*g));
    if (!g)
        return NULL;
    g->key = key;
    int width = x1 - x0;
    int height = y1 - y0;
    if (width > 0 && height > 0 && width <= 2048 && height <= 2048)
    {
        g->mask = malloc((size_t)width * height);
        if (!g->mask)
        {
            free(g);
            return NULL;
        }
        stbtt_MakeGlyphBitmapSubpixel(info, g->mask, width, height, width, face->scale,
                                      face->scale, shift, 0.0f, glyph);
        g->left = (int16_t)x0;
        g->top = (int16_t)y0;
        g->width = (uint16_t)width;
        g->height = (uint16_t)height;
    }
    return g;
}

const bragi_glyph_t *bragi_glyph(bragi_face_t *face, int glyph, int subpixel)
{
    uint32_t key = (uint32_t)glyph * BRAGI_SUBPIXELS + (uint32_t)subpixel;
    bragi_glyph_t **bucket = &face->hash[hash_of(key)];
    for (bragi_glyph_t *g = *bucket; g; g = g->hash_next)
    {
        if (g->key == key)
        {
            face->stats.hits++;
            unlink_use(face, g);
            link_newest(face, g);
            return g;
        }
    }

    bragi_glyph_t *g = make_glyph(face, glyph, subpixel, key);
    if (!g)
        return NULL;
    face->stats.misses++;
    g->hash_next = *bucket;
    *bucket = g;
    link_newest(face, g);
    face->stats.bytes += glyph_cost(g);

    // stay in the budget, but always keep the glyph just made
    while (face->stats.bytes > face->budget && face->oldest != g)
        drop(face, face->oldest);
    return g;
}

void bragi_blend_mask(gfx_buffer_t *buf, int32_t x, int32_t y, const uint8_t *mask,
                      int32_t width, int32_t height, int32_t stride, gfx_color_t color)
{
    if (gfx_alpha(color) == 0)
        return;
    gfx_rect_t area = gfx_rect_intersect(gfx_rect(x, y, width, height), buf->clip);
    for (int32_t py = area.y; py < area.y + area.h; py++)
    {
        const uint8_t *m = mask + (ptrdiff_t)(py - y) * stride + (area.x - x);
        uint32_t *p = gfx_pixel_at(buf, area.x, py);
        for (int32_t i = 0; i < area.w; i++)
        {
            if (m[i] != 0)
                p[i] = gfx_over(p[i], gfx_fade(color, m[i]));
        }
    }
}

/**
 * for the host tests: set a face's cache budget.
 */
void bragi_test_set_budget(bragi_face_t *face, uint32_t budget);
void bragi_test_set_budget(bragi_face_t *face, uint32_t budget)
{
    face->budget = budget;
}
