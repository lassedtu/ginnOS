/**
 * @file bragi_text.c
 * @brief bragi: UTF-8, measuring and drawing lines, wrapping, text boxes.
 */

#include "bragi_internal.h"

#include <string.h>

#define REPLACEMENT 0xFFFDu
#define ELLIPSIS 0x2026u

uint32_t bragi_utf8_next(const char **text, const char *end)
{
    const uint8_t *s = (const uint8_t *)*text;
    const uint8_t *e = (const uint8_t *)end;
    uint32_t c = s[0];
    int extra;
    uint32_t min;
    if (c < 0x80)
    {
        *text += 1;
        return c;
    }
    else if ((c & 0xE0) == 0xC0)
    {
        extra = 1;
        min = 0x80;
        c &= 0x1F;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        extra = 2;
        min = 0x800;
        c &= 0x0F;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        extra = 3;
        min = 0x10000;
        c &= 0x07;
    }
    else
    {
        *text += 1;
        return REPLACEMENT;
    }

    if (e - s <= extra)
    {
        *text += 1;
        return REPLACEMENT;
    }
    for (int i = 1; i <= extra; i++)
    {
        if ((s[i] & 0xC0) != 0x80)
        {
            *text += 1;
            return REPLACEMENT;
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    // too long a form, a surrogate, or past the last code point
    if (c < min || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF)
    {
        *text += 1;
        return REPLACEMENT;
    }
    *text += extra + 1;
    return c;
}

static const char *end_of(const char *text, int32_t length)
{
    return text + (length < 0 ? (int32_t)strlen(text) : length);
}

/**
 * a walk along a line of text: the pen moves by kerning before a glyph,
 * and by its advance and the tracking after it (all in 1/64 pixels).
 */
typedef struct
{
    bragi_face_t *face;       // the face asked for
    int32_t pen;
    bragi_face_t *glyph_face; // the face and the glyph of the last character
    int glyph;
} walk_t;

static void walk_glyph(walk_t *w, uint32_t c)
{
    int glyph;
    bragi_face_t *f = bragi_find_glyph(w->face, c, &glyph);
    if (f == w->glyph_face)
        w->pen += bragi_kerning(f, w->glyph, glyph);
    w->glyph_face = f;
    w->glyph = glyph;
}

static void walk_advance(walk_t *w)
{
    w->pen += bragi_advance(w->glyph_face, w->glyph) + w->face->tracking;
}

/**
 * walk a line of text; with buf, the glyphs are drawn too.
 * @return the pen's end position in 1/64 pixels.
 */
static int32_t run(gfx_buffer_t *buf, bragi_face_t *face, int32_t pen, int32_t baseline,
                   const char *text, const char *end, gfx_color_t color)
{
    walk_t w = {face, pen, NULL, 0};
    while (text < end)
    {
        walk_glyph(&w, bragi_utf8_next(&text, end));
        if (buf)
        {
            // the whole pixel and the nearest quarter
            int32_t px = w.pen >> 6;
            int32_t sub = ((w.pen & 63) * BRAGI_SUBPIXELS + 32) >> 6;
            if (sub == BRAGI_SUBPIXELS)
            {
                px++;
                sub = 0;
            }
            const bragi_glyph_t *g = bragi_glyph(w.glyph_face, w.glyph, sub);
            if (g && g->mask)
                bragi_blend_mask(buf, px + g->left, baseline + g->top, g->mask, g->width,
                                 g->height, g->width, color);
        }
        walk_advance(&w);
    }
    return w.pen;
}

static int32_t units_to_pixels(int32_t units)
{
    return (units + BRAGI_UNITS - 1) >> 6;
}

int32_t bragi_measure(bragi_face_t *face, const char *text, int32_t length)
{
    return units_to_pixels(run(NULL, face, 0, 0, text, end_of(text, length), 0));
}

int32_t bragi_draw(gfx_buffer_t *buf, bragi_face_t *face, int32_t x, int32_t baseline,
                   const char *text, int32_t length, gfx_color_t color)
{
    int32_t pen = run(buf, face, x * BRAGI_UNITS, baseline, text, end_of(text, length), color);
    return units_to_pixels(pen - x * BRAGI_UNITS);
}

int32_t bragi_caret_x(bragi_face_t *face, const char *text, int32_t length, int32_t offset)
{
    const char *end = end_of(text, length);
    if (offset < 0)
        offset = 0;
    if (offset > end - text)
        offset = (int32_t)(end - text);
    return (run(NULL, face, 0, 0, text, text + offset, 0) + BRAGI_UNITS / 2) >> 6;
}

int32_t bragi_offset_at(bragi_face_t *face, const char *text, int32_t length, int32_t x)
{
    const char *end = end_of(text, length);
    const char *s = text;
    walk_t w = {face, 0, NULL, 0};
    while (s < end)
    {
        const char *start = s;
        walk_glyph(&w, bragi_utf8_next(&s, end));
        int32_t before = w.pen;
        walk_advance(&w);
        // the caret goes before a character when x is on its left half
        if (x * BRAGI_UNITS < (before + w.pen) / 2)
            return (int32_t)(start - text);
    }
    return (int32_t)(end - text);
}

int32_t bragi_wrap(bragi_face_t *face, const char *text, int32_t width, const char **next)
{
    const char *line_end = NULL; // the end of the last word that fits
    const char *s = text;
    while (*s && *s != '\n')
    {
        // the next word, and the spaces before it
        const char *word = s;
        while (*word == ' ')
            word++;
        const char *word_end = word;
        while (*word_end && *word_end != ' ' && *word_end != '\n')
            word_end++;

        if (bragi_measure(face, text, (int32_t)(word_end - text)) <= width || word == word_end)
        {
            if (word != word_end)
                line_end = word_end;
            s = word_end;
            continue;
        }
        if (line_end)
            break; // the word goes to the next line

        // one word wider than the line: as many characters as fit (at least one)
        const char *fit = word;
        const char *c = word;
        bragi_utf8_next(&c, word_end);
        fit = c;
        while (c < word_end)
        {
            const char *after = c;
            bragi_utf8_next(&after, word_end);
            if (bragi_measure(face, text, (int32_t)(after - text)) > width)
                break;
            fit = c = after;
        }
        *next = fit;
        return (int32_t)(fit - text);
    }

    const char *after = line_end ? line_end : text;
    while (*after == ' ')
        after++;
    if (*after == '\n')
        after++;
    *next = after;
    if (!line_end)
        return 0;
    return (int32_t)(line_end - text);
}

int32_t bragi_measure_box(bragi_face_t *face, const char *text, int32_t width)
{
    int32_t lines = 0;
    while (*text)
    {
        const char *next;
        bragi_wrap(face, text, width, &next);
        text = next;
        lines++;
    }
    return lines * face->line_height;
}

/**
 * draw a line with "..." at its end, cut to fit the width.
 */
static int32_t draw_cut(gfx_buffer_t *buf, bragi_face_t *face, int32_t x, int32_t baseline,
                        const char *text, int32_t width, gfx_color_t color, bool draw)
{
    const char *dots = bragi_has_char(face, ELLIPSIS) ? "\xE2\x80\xA6" : "...";
    int32_t room = width - bragi_measure(face, dots, -1);
    const char *end = text;
    const char *c = text;
    while (*c && *c != '\n')
    {
        bragi_utf8_next(&c, c + strlen(c));
        if (bragi_measure(face, text, (int32_t)(c - text)) > room)
            break;
        end = c;
    }
    while (end > text && end[-1] == ' ')
        end--;
    int32_t length = (int32_t)(end - text);
    int32_t total = bragi_measure(face, text, length) + bragi_measure(face, dots, -1);
    if (draw)
    {
        int32_t w = bragi_draw(buf, face, x, baseline, text, length, color);
        bragi_draw(buf, face, x + w, baseline, dots, -1, color);
    }
    return total;
}

int32_t bragi_draw_box(gfx_buffer_t *buf, bragi_face_t *face, gfx_rect_t box, const char *text,
                       bragi_align_t align, gfx_color_t color)
{
    int32_t lh = face->line_height;
    int32_t max_lines = box.h / lh;
    int32_t y = 0;
    for (int32_t line = 0; line < max_lines && *text; line++, y += lh)
    {
        const char *next;
        int32_t length = bragi_wrap(face, text, box.w, &next);
        int32_t baseline = box.y + y + face->ascent;
        bool last = line == max_lines - 1 && *next;
        int32_t width = last ? draw_cut(NULL, face, 0, 0, text, box.w, color, false)
                             : bragi_measure(face, text, length);
        int32_t x = box.x;
        if (align == BRAGI_ALIGN_CENTER)
            x += (box.w - width) / 2;
        else if (align == BRAGI_ALIGN_RIGHT)
            x += box.w - width;

        if (last)
            draw_cut(buf, face, x, baseline, text, box.w, color, true);
        else
            bragi_draw(buf, face, x, baseline, text, length, color);
        text = next;
    }
    return y;
}
