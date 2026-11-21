/**
 * @file bragi_cache.c
 * @brief bragi: the glyph cache of a face, and blending in linear light.
 *
 * each face keeps the glyph images it made, by glyph and subpixel
 * position, in a hash table. a list in use order lets the cache drop the
 * glyph used least recently when the masks use more than the budget.
 */

#include "bragi_internal.h"

#include <stdlib.h>
#include <string.h>

/* ---- the cache ------------------------------------------------------------ */

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

/* ---- blending in linear light --------------------------------------------- */

// sRGB byte to linear light, 0 to 4095 (made with the sRGB formula)
static const uint16_t to_linear[256] = {
       0,    1,    2,    4,    5,    6,    7,    9,   10,   11,   12,   14,
      15,   16,   18,   20,   21,   23,   25,   27,   29,   31,   33,   35,
      37,   40,   42,   45,   48,   50,   53,   56,   59,   62,   66,   69,
      72,   76,   79,   83,   87,   91,   95,   99,  103,  107,  112,  116,
     121,  126,  131,  136,  141,  146,  151,  156,  162,  168,  173,  179,
     185,  191,  197,  204,  210,  216,  223,  230,  237,  244,  251,  258,
     265,  273,  280,  288,  296,  304,  312,  320,  329,  337,  346,  354,
     363,  372,  381,  390,  400,  409,  419,  428,  438,  448,  458,  469,
     479,  490,  500,  511,  522,  533,  544,  555,  567,  578,  590,  602,
     614,  626,  639,  651,  664,  676,  689,  702,  715,  728,  742,  755,
     769,  783,  797,  811,  825,  840,  854,  869,  884,  899,  914,  929,
     945,  960,  976,  992, 1008, 1024, 1041, 1057, 1074, 1091, 1108, 1125,
    1142, 1159, 1177, 1195, 1213, 1231, 1249, 1267, 1286, 1304, 1323, 1342,
    1361, 1381, 1400, 1420, 1440, 1459, 1480, 1500, 1520, 1541, 1562, 1582,
    1603, 1625, 1646, 1668, 1689, 1711, 1733, 1755, 1778, 1800, 1823, 1846,
    1869, 1892, 1916, 1939, 1963, 1987, 2011, 2035, 2059, 2084, 2109, 2133,
    2159, 2184, 2209, 2235, 2260, 2286, 2312, 2339, 2365, 2392, 2419, 2446,
    2473, 2500, 2527, 2555, 2583, 2611, 2639, 2668, 2696, 2725, 2754, 2783,
    2812, 2841, 2871, 2901, 2931, 2961, 2991, 3022, 3052, 3083, 3114, 3146,
    3177, 3209, 3240, 3272, 3304, 3337, 3369, 3402, 3435, 3468, 3501, 3535,
    3568, 3602, 3636, 3670, 3705, 3739, 3774, 3809, 3844, 3879, 3915, 3950,
    3986, 4022, 4059, 4095,
};

// coverage made stronger for text darker than its background: in linear
// light, dark text on a light background looks thinner than light text on
// a dark one. 255 * (c / 255) ^ (1 / 1.4)
static const uint8_t darker[256] = {
      0,   5,   8,  11,  13,  15,  18,  20,  22,  23,  25,  27,  29,  30,  32,  34,
     35,  37,  38,  40,  41,  43,  44,  46,  47,  49,  50,  51,  53,  54,  55,  57,
     58,  59,  60,  62,  63,  64,  65,  67,  68,  69,  70,  72,  73,  74,  75,  76,
     77,  78,  80,  81,  82,  83,  84,  85,  86,  87,  89,  90,  91,  92,  93,  94,
     95,  96,  97,  98,  99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
    111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126,
    127, 128, 129, 130, 131, 132, 133, 133, 134, 135, 136, 137, 138, 139, 140, 141,
    142, 143, 143, 144, 145, 146, 147, 148, 149, 150, 151, 151, 152, 153, 154, 155,
    156, 157, 158, 158, 159, 160, 161, 162, 163, 164, 164, 165, 166, 167, 168, 169,
    170, 170, 171, 172, 173, 174, 175, 175, 176, 177, 178, 179, 180, 180, 181, 182,
    183, 184, 184, 185, 186, 187, 188, 188, 189, 190, 191, 192, 192, 193, 194, 195,
    196, 196, 197, 198, 199, 200, 200, 201, 202, 203, 204, 204, 205, 206, 207, 207,
    208, 209, 210, 211, 211, 212, 213, 214, 214, 215, 216, 217, 217, 218, 219, 220,
    220, 221, 222, 223, 223, 224, 225, 226, 226, 227, 228, 229, 229, 230, 231, 232,
    232, 233, 234, 235, 235, 236, 237, 238, 238, 239, 240, 241, 241, 242, 243, 243,
    244, 245, 246, 246, 247, 248, 249, 249, 250, 251, 251, 252, 253, 254, 254, 255,
};

// linear light (0 to 4095) to the nearest sRGB byte, made from to_linear
static uint8_t to_srgb[4096];
static bool to_srgb_made;

static void make_to_srgb(void)
{
    int s = 0;
    for (int l = 0; l < 4096; l++)
    {
        // move on while the next byte is at least as near
        while (s < 255 && to_linear[s + 1] - l <= l - to_linear[s])
            s++;
        to_srgb[l] = (uint8_t)s;
    }
    to_srgb_made = true;
}

static uint32_t mix_channel(uint32_t dst, uint32_t src, uint32_t a)
{
    int32_t d = to_linear[dst];
    int32_t s = to_linear[src];
    int32_t l = d + ((s - d) * (int32_t)a + (s >= d ? 127 : -127)) / 255;
    return to_srgb[l];
}

void bragi_blend_mask(gfx_buffer_t *buf, int32_t x, int32_t y, const uint8_t *mask,
                      int32_t width, int32_t height, int32_t stride, gfx_color_t color)
{
    if (!to_srgb_made)
        make_to_srgb();
    uint32_t alpha = gfx_alpha(color);
    if (alpha == 0)
        return;

    // the colour's straight channels
    uint32_t cr = (color >> 16) & 0xFF;
    uint32_t cg = (color >> 8) & 0xFF;
    uint32_t cb = color & 0xFF;
    if (alpha < 255)
    {
        cr = cr * 255 / alpha;
        cg = cg * 255 / alpha;
        cb = cb * 255 / alpha;
    }

    // the colour's brightness, from the linear channels
    uint32_t text_light = to_linear[cr] * 2u + to_linear[cg] * 5u + to_linear[cb];

    gfx_rect_t area = gfx_rect_intersect(gfx_rect(x, y, width, height), buf->clip);
    for (int32_t py = area.y; py < area.y + area.h; py++)
    {
        const uint8_t *m = mask + (ptrdiff_t)(py - y) * stride + (area.x - x);
        uint32_t *p = gfx_pixel_at(buf, area.x, py);
        for (int32_t i = 0; i < area.w; i++)
        {
            if (m[i] == 0)
                continue;
            uint32_t d = p[i];
            uint32_t dst_light = to_linear[(d >> 16) & 0xFF] * 2u +
                                 to_linear[(d >> 8) & 0xFF] * 5u + to_linear[d & 0xFF];
            uint32_t cover = text_light < dst_light ? darker[m[i]] : m[i];
            uint32_t a = (cover * alpha + 127) / 255;
            if (gfx_alpha(d) != 255)
            {
                // not opaque below: blend as libgfx does
                p[i] = gfx_over(d, gfx_fade(color, (uint8_t)m[i]));
                continue;
            }
            p[i] = 0xFF000000u | mix_channel((d >> 16) & 0xFF, cr, a) << 16 |
                   mix_channel((d >> 8) & 0xFF, cg, a) << 8 | mix_channel(d & 0xFF, cb, a);
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
