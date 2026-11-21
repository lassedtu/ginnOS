/**
 * @file bragi_test.c
 * @brief host tests of bragi (make host-test), with Inter and JetBrains Mono.
 *
 * the glyph shapes come from stb_truetype's rasterizer, which uses
 * floating point: the golden images are those of the host, and another
 * host can differ by a level here and there.
 */

#include "bragi.h"
#include "hosttest.h"

#include <stdlib.h>
#include <string.h>

static bragi_font_t *inter;
static bragi_font_t *mono;

static void golden(const char *name, const gfx_buffer_t *buf)
{
    uint8_t *rgb = malloc((size_t)buf->width * buf->height * 3);
    for (int32_t y = 0; y < buf->height; y++)
    {
        for (int32_t x = 0; x < buf->width; x++)
        {
            uint32_t p = *gfx_pixel_at(buf, x, y);
            uint8_t *out = rgb + ((size_t)y * buf->width + x) * 3;
            out[0] = (uint8_t)(p >> 16);
            out[1] = (uint8_t)(p >> 8);
            out[2] = (uint8_t)p;
        }
    }
    hosttest_golden(name, rgb, buf->width, buf->height);
    free(rgb);
}

static uint32_t decode_one(const char *s, size_t n, size_t *used)
{
    const char *p = s;
    uint32_t c = bragi_utf8_next(&p, s + n);
    *used = (size_t)(p - s);
    return c;
}

static void test_utf8(void)
{
    size_t used;
    CHECK_EQ(decode_one("A", 1, &used), 'A');
    CHECK_EQ(used, 1u);
    CHECK_EQ(decode_one("\xC3\xA6", 2, &used), 0xE6u); // ae
    CHECK_EQ(used, 2u);
    CHECK_EQ(decode_one("\xE2\x82\xAC", 3, &used), 0x20ACu); // euro sign
    CHECK_EQ(decode_one("\xF0\x9F\x99\x82", 4, &used), 0x1F642u);
    CHECK_EQ(used, 4u);

    // bad sequences: one U+FFFD, and one byte used
    CHECK_EQ(decode_one("\xC0\x80", 2, &used), 0xFFFDu);  // too long a form of 0
    CHECK_EQ(used, 1u);
    CHECK_EQ(decode_one("\xED\xA0\x80", 3, &used), 0xFFFDu); // a surrogate
    CHECK_EQ(decode_one("\xE2\x82", 2, &used), 0xFFFDu);     // cut off
    CHECK_EQ(used, 1u);
    CHECK_EQ(decode_one("\xC3" "A", 2, &used), 0xFFFDu);     // no continuation
    CHECK_EQ(decode_one("\x80", 1, &used), 0xFFFDu);
    CHECK_EQ(decode_one("\xF4\x90\x80\x80", 4, &used), 0xFFFDu); // past U+10FFFF
}

static void test_fonts(void)
{
    CHECK(bragi_font_open(FONT_DIR "/no-such-font.ttf") == NULL);
    static const uint8_t junk[64] = "not a font";
    CHECK(bragi_font_from_memory(junk, sizeof(junk), false) == NULL);
    CHECK(bragi_face_new(inter, 2.0f) == NULL);
    CHECK(bragi_face_new(inter, 1000.0f) == NULL);

    bragi_face_t *face = bragi_face_new(inter, 16.0f);
    CHECK(bragi_ascent(face) >= 12 && bragi_ascent(face) <= 18);
    CHECK(bragi_descent(face) >= 2 && bragi_descent(face) <= 6);
    CHECK(bragi_line_height(face) >= bragi_ascent(face) + bragi_descent(face));
    CHECK(bragi_has_char(face, 'a') && bragi_has_char(face, 0xE6));
    CHECK(!bragi_has_char(face, 0x10FFFD));
    bragi_face_free(face);
}

static void test_measure(void)
{
    bragi_face_t *face = bragi_face_new(inter, 16.0f);
    CHECK_EQ(bragi_measure(face, "", -1), 0);
    int32_t hello = bragi_measure(face, "Hello", -1);
    CHECK(hello > 20 && hello < 60);
    CHECK_EQ(bragi_measure(face, "Hello, world", 5), hello);
    // longer text is wider; kerning pulls "AV" together
    CHECK(bragi_measure(face, "Hello, world", -1) > hello);
    CHECK(bragi_measure(face, "AV", -1) <= bragi_measure(face, "A", -1) + bragi_measure(face, "V", -1));
    bragi_face_free(face);

    // a monospace font: every character has the same advance
    bragi_face_t *m = bragi_face_new(mono, 15.0f);
    CHECK_EQ(bragi_measure(m, "iiiiiiiiii", -1), bragi_measure(m, "MMMMMMMMMM", -1));
    int32_t cw;
    int32_t ch;
    bragi_cell_size(m, &cw, &ch);
    CHECK(cw >= 8 && cw <= 10);
    CHECK_EQ(ch, bragi_line_height(m));
    bragi_face_free(m);
}

static void test_wrap(void)
{
    bragi_face_t *face = bragi_face_new(inter, 16.0f);
    const char *text = "the quick brown fox";
    // wide enough for each line, not for "the quick brown"
    int32_t width = bragi_measure(face, "the quick", -1);
    if (bragi_measure(face, "brown fox", -1) > width)
        width = bragi_measure(face, "brown fox", -1);
    CHECK(bragi_measure(face, "the quick brown", -1) > width);
    const char *next;

    int32_t n = bragi_wrap(face, text, width, &next);
    CHECK_EQ(n, 9);
    CHECK(strcmp(next, "brown fox") == 0);
    n = bragi_wrap(face, next, width, &next);
    CHECK_EQ(n, 9);
    CHECK_EQ(*next, '\0');

    // a newline ends a line, and an empty line is a line
    n = bragi_wrap(face, "one\n\ntwo", 1000, &next);
    CHECK_EQ(n, 3);
    n = bragi_wrap(face, next, 1000, &next);
    CHECK_EQ(n, 0);
    CHECK(strcmp(next, "two") == 0);

    // a word wider than the line breaks inside, one character at least
    n = bragi_wrap(face, "abcdefghijklmnop", bragi_measure(face, "abcd", -1), &next);
    CHECK_EQ(n, 4);
    n = bragi_wrap(face, "\xC3\xA6\xC3\xA6", 1, &next);
    CHECK_EQ(n, 2);

    CHECK_EQ(bragi_measure_box(face, text, width), 2 * bragi_line_height(face));
    CHECK_EQ(bragi_measure_box(face, "", width), 0);
    bragi_face_free(face);
}

static void test_cache(void)
{
    bragi_face_t *face = bragi_face_new(inter, 24.0f);
    gfx_buffer_t *buf = gfx_buffer_new(400, 40);
    bragi_stats_t st;
    bragi_draw(buf, face, 0, 30, "abcabc", -1, gfx_rgb(255, 255, 255));
    bragi_face_stats(face, &st);
    CHECK_EQ(st.hits + st.misses, 6u);
    CHECK(st.hits >= 1); // a glyph at the same quarter pixel comes back

    // a very small budget: glyphs are dropped, and drawing still works
    bragi_face_t *small = bragi_face_new(inter, 24.0f);
    extern void bragi_test_set_budget(bragi_face_t *, uint32_t);
    bragi_test_set_budget(small, 1000);
    bragi_draw(buf, small, 0, 30, "the quick brown fox jumps", -1, gfx_rgb(255, 255, 255));
    bragi_face_stats(small, &st);
    CHECK(st.evicted > 0);
    CHECK(st.bytes <= 1000 || st.evicted > 0);
    bragi_face_free(small);
    bragi_face_free(face);
    gfx_buffer_free(buf);
}

static void test_fallback(void)
{
    bragi_face_t *sans = bragi_face_new(inter, 16.0f);
    bragi_face_t *m = bragi_face_new(mono, 16.0f);
    // a character JetBrains Mono has and Inter does not
    static const uint32_t candidates[] = {0xE0B0, 0x2591, 0x23BA, 0x2387, 0x25F8};
    uint32_t c = 0;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        if (bragi_has_char(m, candidates[i]) && !bragi_has_char(sans, candidates[i]))
        {
            c = candidates[i];
            break;
        }
    }
    CHECK(c != 0);
    char s[4] = {(char)(0xE0 | c >> 12), (char)(0x80 | ((c >> 6) & 0x3F)),
                 (char)(0x80 | (c & 0x3F)), 0};
    int32_t missing = bragi_measure(sans, s, -1);
    bragi_face_set_fallback(sans, m);
    CHECK_EQ(bragi_measure(sans, s, -1), bragi_measure(m, s, -1));
    CHECK(missing > 0); // the "missing glyph" box has a width too
    bragi_face_free(sans);
    bragi_face_free(m);
}

/**
 * text at several sizes, dark on light and light on dark, a box with
 * wrapping and "...", and a terminal grid with box drawing.
 */
static void test_scene(void)
{
    gfx_buffer_t *buf = gfx_buffer_new(480, 360);
    gfx_fill_rect(buf, gfx_rect(0, 0, 240, 200), gfx_rgb(250, 250, 252));
    gfx_fill_rect(buf, gfx_rect(240, 0, 240, 200), gfx_rgb(24, 26, 32));

    static const float sizes[] = {11, 13, 16, 22, 32};
    int32_t y = 4;
    for (size_t i = 0; i < 5; i++)
    {
        bragi_face_t *f = bragi_face_new(inter, sizes[i]);
        y += bragi_ascent(f);
        bragi_draw(buf, f, 8, y, "Hamburgefonstiv", -1, gfx_rgb(20, 20, 24));
        bragi_draw(buf, f, 248, y, "Hamburgefonstiv", -1, gfx_rgb(235, 235, 240));
        y += bragi_descent(f) + 4;
        bragi_face_free(f);
    }
    bragi_face_t *body = bragi_face_new(inter, 13.0f);
    bragi_draw(buf, body, 8, y + 12, "\xC3\x86\xC3\x98\xC3\x85 \xC3\xA6\xC3\xB8\xC3\xA5 caf\xC3\xA9 \xE2\x82\xAC 12", -1,
               gfx_rgb(200, 40, 40));

    // a box: wrapped, centred, cut off with an ellipsis
    gfx_rect_t box = gfx_rect(248, y - 4, 220, 3 * bragi_line_height(body));
    gfx_fill_rect(buf, box, gfx_rgb(50, 54, 66));
    bragi_draw_box(buf, body, box,
                   "Bragi is the font library of ginnOS. It wraps text at spaces, keeps the "
                   "spacing even, and ends with an ellipsis when the text does not fit.",
                   BRAGI_ALIGN_CENTER, gfx_rgb(255, 255, 255));
    bragi_face_free(body);

    // a terminal grid: box drawing, blocks and text in JetBrains Mono
    gfx_fill_rect(buf, gfx_rect(0, 200, 480, 160), gfx_rgb(16, 18, 24));
    bragi_face_t *m = bragi_face_new(mono, 14.0f);
    int32_t cw;
    int32_t ch;
    bragi_cell_size(m, &cw, &ch);
    static const char *rows[] = {
        "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x80\xE2\x94\xAC\xE2\x94\x80\xE2\x94\x80\xE2\x94\x90 "
        "\xE2\x94\x8F\xE2\x94\x81\xE2\x94\x81\xE2\x94\x93 \xE2\x95\x94\xE2\x95\x90\xE2\x95\x97 "
        "\xE2\x95\xAD\xE2\x94\x80\xE2\x95\xAE",
        "\xE2\x94\x82" "ab\xE2\x94\x82" "cd\xE2\x94\x82 \xE2\x94\x83ok\xE2\x94\x83 \xE2\x95\x91x\xE2\x95\x91 "
        "\xE2\x94\x82y\xE2\x94\x82",
        "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80\xE2\x94\xBC\xE2\x94\x80\xE2\x94\x80\xE2\x94\xA4 "
        "\xE2\x94\x97\xE2\x94\x81\xE2\x94\x81\xE2\x94\x9B \xE2\x95\x9A\xE2\x95\x90\xE2\x95\x9D "
        "\xE2\x95\xB0\xE2\x94\x80\xE2\x95\xAF",
        "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\xB4\xE2\x94\x80\xE2\x94\x80\xE2\x94\x98 "
        "\xE2\x96\x81\xE2\x96\x82\xE2\x96\x83\xE2\x96\x84\xE2\x96\x85\xE2\x96\x86\xE2\x96\x87\xE2\x96\x88 "
        "\xE2\x96\x91\xE2\x96\x92\xE2\x96\x93\xE2\x96\x88 \xE2\x96\x9A\xE2\x96\x9E\xE2\x96\x99",
        "$ ls -la /usr/share/fonts  # {}[]()<>=>!= 0O1lI",
    };
    for (int32_t r = 0; r < 5; r++)
    {
        const char *s = rows[r];
        const char *end = s + strlen(s);
        for (int32_t col = 0; s < end; col++)
        {
            uint32_t c = bragi_utf8_next(&s, end);
            bragi_draw_cell(buf, m, 8 + col * cw, 208 + r * ch, c,
                            r == 4 ? gfx_rgb(120, 220, 140) : gfx_rgb(210, 214, 224));
        }
    }
    bragi_face_free(m);
    golden("scene", buf);
    gfx_buffer_free(buf);
}

int main(void)
{
    inter = bragi_font_open(FONT_DIR "/inter/Inter-Regular.ttf");
    mono = bragi_font_open(FONT_DIR "/jetbrains-mono/JetBrainsMono-Regular.ttf");
    CHECK(inter != NULL && mono != NULL);
    if (!inter || !mono)
        return hosttest_done("bragi");

    test_utf8();
    test_fonts();
    test_measure();
    test_wrap();
    test_cache();
    test_fallback();
    test_scene();
    bragi_font_close(inter);
    bragi_font_close(mono);
    return hosttest_done("bragi");
}
