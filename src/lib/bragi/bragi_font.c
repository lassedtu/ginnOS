/**
 * @file bragi_font.c
 * @brief bragi: fonts, faces, glyph lookup and metrics (stb_truetype).
 *
 * stb_truetype is compiled here, and only bragi uses it, so it can be
 * replaced later without changing the apps.
 */

#define STB_TRUETYPE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "bragi_internal.h"
#pragma GCC diagnostic pop

#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FONT_MAX_SIZE (32u * 1024 * 1024)

bragi_font_t *bragi_font_from_memory(const uint8_t *data, size_t size, bool take)
{
    if (!data || size < 12 || size > FONT_MAX_SIZE)
        return NULL;
    int offset = stbtt_GetFontOffsetForIndex(data, 0);
    bragi_font_t *font = calloc(1, sizeof(*font));
    if (!font || offset < 0 || !stbtt_InitFont(&font->info, data, offset))
    {
        free(font);
        return NULL;
    }
    font->data = (uint8_t *)data;
    font->owns_data = take;
    return font;
}

bragi_font_t *bragi_font_open(const char *path)
{
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0 || st.st_size <= 0 ||
        (size_t)st.st_size > FONT_MAX_SIZE)
    {
        if (fd >= 0)
            close(fd);
        return NULL;
    }
    size_t size = (size_t)st.st_size;
    uint8_t *data = malloc(size);
    size_t done = 0;
    while (data && done < size)
    {
        ssize_t n = read(fd, data + done, size - done);
        if (n <= 0)
            break;
        done += (size_t)n;
    }
    close(fd);
    if (!data || done != size)
    {
        free(data);
        return NULL;
    }
    bragi_font_t *font = bragi_font_from_memory(data, size, true);
    if (!font)
        free(data);
    return font;
}

void bragi_font_close(bragi_font_t *font)
{
    if (!font)
        return;
    if (font->owns_data)
        free(font->data);
    free(font);
}

bragi_face_t *bragi_face_new(bragi_font_t *font, float pixel_size)
{
    if (!font || !(pixel_size >= 4.0f && pixel_size <= 512.0f))
        return NULL;
    bragi_face_t *face = calloc(1, sizeof(*face));
    if (!face)
        return NULL;
    face->font = font;
    face->scale = stbtt_ScaleForMappingEmToPixels(&font->info, pixel_size);
    face->budget = BRAGI_CACHE_BUDGET;

    int ascent;
    int descent;
    int line_gap;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);
    face->ascent = (int32_t)ceil(ascent * face->scale);
    face->descent = (int32_t)ceil(-descent * face->scale);
    face->line_height = (int32_t)ceil((ascent - descent + line_gap) * face->scale);
    return face;
}

void bragi_face_free(bragi_face_t *face)
{
    if (!face)
        return;
    bragi_glyph_t *g = face->newest;
    while (g)
    {
        bragi_glyph_t *next = g->older;
        free(g->mask);
        free(g);
        g = next;
    }
    free(face);
}

void bragi_face_set_fallback(bragi_face_t *face, bragi_face_t *fallback)
{
    face->fallback = fallback;
}

int32_t bragi_ascent(const bragi_face_t *face)
{
    return face->ascent;
}

int32_t bragi_descent(const bragi_face_t *face)
{
    return face->descent;
}

int32_t bragi_line_height(const bragi_face_t *face)
{
    return face->line_height;
}

bool bragi_has_char(const bragi_face_t *face, uint32_t codepoint)
{
    return stbtt_FindGlyphIndex(&face->font->info, (int)codepoint) != 0;
}

bragi_face_t *bragi_find_glyph(bragi_face_t *face, uint32_t codepoint, int *glyph)
{
    // a fallback chain is short; the depth limit stops a loop in it
    bragi_face_t *f = face;
    for (int depth = 0; f && depth < 8; depth++, f = f->fallback)
    {
        int index = stbtt_FindGlyphIndex(&f->font->info, (int)codepoint);
        if (index != 0)
        {
            *glyph = index;
            return f;
        }
    }
    *glyph = 0;
    return face;
}

int32_t bragi_advance(const bragi_face_t *face, int glyph)
{
    int advance;
    int bearing;
    stbtt_GetGlyphHMetrics(&face->font->info, glyph, &advance, &bearing);
    return (int32_t)floor(advance * face->scale * BRAGI_UNITS + 0.5);
}

int32_t bragi_kerning(const bragi_face_t *face, int left, int right)
{
    int kern = stbtt_GetGlyphKernAdvance(&face->font->info, left, right);
    return kern == 0 ? 0 : (int32_t)floor(kern * face->scale * BRAGI_UNITS + 0.5);
}

void bragi_face_stats(const bragi_face_t *face, bragi_stats_t *stats)
{
    *stats = face->stats;
}
