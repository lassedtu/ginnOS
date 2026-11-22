/**
 * @file vt_test.c
 * @brief host tests of libvt (make host-test): bytes in, screen text out.
 */

#include "hosttest.h"
#include "vt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put(vt_t *vt, const char *text)
{
    vt_write(vt, text, strlen(text));
}

/** the text of line y (screen or scrollback), without the empty end */
static const char *line_text(const vt_t *vt, int y)
{
    static char buf[1024];
    const vt_line_t *line = vt_line(vt, y);
    if (!line)
        return "(none)";
    // a wrapped line would not end; take the cells up to its end only
    vt_text(vt, 0, y, line->cols - 1, y, buf, sizeof(buf));
    size_t n = strlen(buf);
    while (n && buf[n - 1] == ' ')
        buf[--n] = '\0';
    return buf;
}

static bool line_is(const vt_t *vt, int y, const char *expected)
{
    const char *got = line_text(vt, y);
    if (strcmp(got, expected) == 0)
        return true;
    fprintf(stderr, "  line %d: got \"%s\", expected \"%s\"\n", y, got, expected);
    return false;
}

static const vt_cell_t *cell(const vt_t *vt, int x, int y)
{
    return &vt_line(vt, y)->cells[x];
}

static char replies[256];

static void on_reply(const char *data, size_t length, void *user)
{
    (void)user;
    size_t used = strlen(replies);
    if (used + length < sizeof(replies))
    {
        memcpy(replies + used, data, length);
        replies[used + length] = '\0';
    }
}

static void test_text_and_wrap(void)
{
    vt_t *vt = vt_new(10, 4, 100);
    put(vt, "hello\r\nworld");
    CHECK(line_is(vt, 0, "hello"));
    CHECK(line_is(vt, 1, "world"));
    CHECK_EQ(vt->cursor_x, 5);
    CHECK_EQ(vt->cursor_y, 1);

    // the 11th character goes to the next line; the first is marked wrapped
    put(vt, "\r\n0123456789AB");
    CHECK(line_is(vt, 2, "0123456789"));
    CHECK(line_is(vt, 3, "AB"));
    CHECK(vt_line(vt, 2)->wrapped);
    char buf[64];
    vt_text(vt, 0, 2, 9, 3, buf, sizeof(buf));
    CHECK(strcmp(buf, "0123456789AB") == 0); // one line, as it was written

    // at the last column the cursor waits: CR does not wrap
    vt_t *w = vt_new(4, 2, 0);
    put(w, "abcd\rX");
    CHECK(line_is(w, 0, "Xbcd"));
    CHECK_EQ(w->cursor_y, 0);
    vt_free(w);
    vt_free(vt);
}

static void test_cursor_and_erase(void)
{
    vt_t *vt = vt_new(10, 5, 0);
    put(vt, "\033[3;4Hx");
    CHECK(line_is(vt, 2, "   x"));
    put(vt, "\033[H\033[2B\033[5Cy"); // down 2, right 5
    CHECK(line_is(vt, 2, "   x y"));
    put(vt, "\033[1;1Habcdefghij\033[1;5H\033[K");
    CHECK(line_is(vt, 0, "abcd"));
    put(vt, "\033[1;3H\033[1K");
    CHECK(line_is(vt, 0, "   d"));
    put(vt, "\033[2J");
    for (int y = 0; y < 5; y++)
        CHECK(line_is(vt, y, ""));
    // the cursor stays on the screen
    put(vt, "\033[99;99H");
    CHECK_EQ(vt->cursor_x, 9);
    CHECK_EQ(vt->cursor_y, 4);
    put(vt, "\033[200A");
    CHECK_EQ(vt->cursor_y, 0);
    vt_free(vt);
}

static void test_insert_delete(void)
{
    vt_t *vt = vt_new(10, 4, 0);
    put(vt, "abcdef\033[1;3H\033[2P");
    CHECK(line_is(vt, 0, "abef"));
    put(vt, "\033[2@");
    CHECK(line_is(vt, 0, "ab  ef"));
    put(vt, "\033[1;1H\033[3X");
    CHECK(line_is(vt, 0, "    ef"));
    put(vt, "\033[4h\033[1;1HZ\033[4l"); // insert mode
    CHECK(line_is(vt, 0, "Z    ef"));

    put(vt, "\033[2J\033[1;1H1\r\n2\r\n3\r\n4");
    put(vt, "\033[2;1H\033[L");
    CHECK(line_is(vt, 0, "1"));
    CHECK(line_is(vt, 1, ""));
    CHECK(line_is(vt, 2, "2"));
    CHECK(line_is(vt, 3, "3"));
    put(vt, "\033[1;1H\033[2M");
    CHECK(line_is(vt, 0, "2"));
    CHECK(line_is(vt, 1, "3"));
    CHECK(line_is(vt, 2, ""));
    CHECK_EQ(vt_scrollback_lines(vt), 0); // deleted lines are gone, not kept
    vt_free(vt);
}

static void test_colors(void)
{
    vt_t *vt = vt_new(20, 2, 0);
    put(vt, "\033[1;31ma\033[0mb\033[38;5;196;48;2;1;2;3mc\033[38:2::10:20:30md\033[94;4me");
    CHECK_EQ(cell(vt, 0, 0)->fg, VT_COLOR_INDEX | 1);
    CHECK_EQ(cell(vt, 0, 0)->attr, VT_BOLD);
    CHECK_EQ(cell(vt, 1, 0)->fg, VT_COLOR_DEFAULT);
    CHECK_EQ(cell(vt, 1, 0)->attr, 0);
    CHECK_EQ(cell(vt, 2, 0)->fg, VT_COLOR_INDEX | 196);
    CHECK_EQ(cell(vt, 2, 0)->bg, VT_COLOR_RGB | 0x010203);
    CHECK_EQ(cell(vt, 3, 0)->fg, VT_COLOR_RGB | 0x0A141E);
    CHECK_EQ(cell(vt, 4, 0)->fg, VT_COLOR_INDEX | 12);
    CHECK_EQ(cell(vt, 4, 0)->attr, VT_UNDERLINE);
    put(vt, "\033[24;7mf\033[27;39;49m");
    CHECK_EQ(cell(vt, 5, 0)->attr, VT_INVERSE);

    // an erase takes the background of the pen
    put(vt, "\033[44m\033[2K\033[0m");
    CHECK_EQ(cell(vt, 10, 0)->bg, VT_COLOR_INDEX | 4);

    CHECK_EQ(vt_palette(1), 0xCD0000);
    CHECK_EQ(vt_palette(16), 0x000000);
    CHECK_EQ(vt_palette(231), 0xFFFFFF);
    CHECK_EQ(vt_palette(232), 0x080808);
    CHECK_EQ(vt_palette(196), 0xFF0000);
    vt_free(vt);
}

static void test_scrollback(void)
{
    vt_t *vt = vt_new(10, 3, 4);
    for (int i = 0; i < 8; i++)
    {
        char line[16];
        snprintf(line, sizeof(line), "%sline%d", i ? "\r\n" : "", i);
        put(vt, line);
    }
    CHECK(line_is(vt, 0, "line5"));
    CHECK(line_is(vt, 2, "line7"));
    CHECK_EQ(vt_scrollback_lines(vt), 4); // 5 went up; the oldest one is gone
    CHECK(line_is(vt, -1, "line4"));
    CHECK(line_is(vt, -4, "line1"));
    CHECK(vt_line(vt, -5) == NULL);
    CHECK_EQ(vt->scrolled, 5);
    vt_clean(vt);
    CHECK_EQ(vt->scrolled, 0);
    CHECK(!vt_line_dirty(vt, 0));
    put(vt, "\033[3J");
    CHECK_EQ(vt_scrollback_lines(vt), 0);
    vt_free(vt);
}

static void test_scroll_region(void)
{
    vt_t *vt = vt_new(10, 5, 10);
    put(vt, "top\033[5;1Hbottom\033[2;4r");
    CHECK_EQ(vt->cursor_y, 0); // DECSTBM homes the cursor
    put(vt, "\033[2;1Ha\r\nb\r\nc\r\nd\r\ne");
    CHECK(line_is(vt, 0, "top"));
    CHECK(line_is(vt, 1, "c"));
    CHECK(line_is(vt, 2, "d"));
    CHECK(line_is(vt, 3, "e"));
    CHECK(line_is(vt, 4, "bottom"));
    CHECK_EQ(vt_scrollback_lines(vt), 0); // a region not at the top keeps nothing
    CHECK_EQ(vt->scrolled, 0);            // and is not a move of the whole screen
    CHECK(vt_line_dirty(vt, 2));

    // reverse index at the top of the region
    put(vt, "\033[2;1H\033M");
    CHECK(line_is(vt, 1, ""));
    CHECK(line_is(vt, 2, "c"));
    CHECK(line_is(vt, 4, "bottom"));

    // origin mode: row 1 is the region's top
    put(vt, "\033[?6h\033[1;1HO\033[?6l");
    CHECK(line_is(vt, 1, "O"));
    vt_free(vt);
}

static void test_alt_screen(void)
{
    vt_t *vt = vt_new(10, 3, 10);
    put(vt, "shell$ \033[?1049h");
    CHECK(vt->alt_screen);
    CHECK(line_is(vt, 0, ""));
    put(vt, "\033[1;1Heditor\r\n\r\n\r\n\r\n"); // scrolls, but keeps nothing
    CHECK_EQ(vt_scrollback_lines(vt), 0);
    put(vt, "\033[?1049l");
    CHECK(!vt->alt_screen);
    CHECK(line_is(vt, 0, "shell$"));
    CHECK_EQ(vt->cursor_x, 7);
    CHECK_EQ(vt->cursor_y, 0);
    vt_free(vt);
}

static void test_utf8(void)
{
    vt_t *vt = vt_new(10, 2, 0);
    const char *text = "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80!"; // e acute, euro, a face
    // one byte at a time: a character can be split anywhere
    for (size_t i = 0; i < strlen(text); i++)
        vt_write(vt, text + i, 1);
    CHECK_EQ(cell(vt, 0, 0)->ch, 0xE9);
    CHECK_EQ(cell(vt, 1, 0)->ch, 0x20AC);
    CHECK_EQ(cell(vt, 2, 0)->ch, 0x1F600);
    CHECK_EQ(cell(vt, 3, 0)->ch, '!');
    char buf[32];
    vt_text(vt, 0, 0, 9, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, text) == 0);

    put(vt, "\r\n\xFF" "a\xC3" "b");
    CHECK_EQ(cell(vt, 0, 1)->ch, 0xFFFD);
    CHECK_EQ(cell(vt, 1, 1)->ch, 'a');
    CHECK_EQ(cell(vt, 2, 1)->ch, 0xFFFD); // cut short by b
    CHECK_EQ(cell(vt, 3, 1)->ch, 'b');
    vt_free(vt);
}

static void test_replies_and_title(void)
{
    vt_t *vt = vt_new(10, 5, 0);
    vt_set_reply(vt, on_reply, NULL);
    replies[0] = '\0';
    put(vt, "\033[3;7H\033[6n\033[5n\033[c");
    CHECK(strcmp(replies, "\033[3;7R\033[0n\033[?62;22c") == 0);

    put(vt, "\033]0;first\007");
    CHECK(strcmp(vt->title, "first") == 0);
    CHECK(vt->title_changed);
    vt->title_changed = false;
    // split, and ended with ST
    put(vt, "\033]2;sec");
    put(vt, "ond\033");
    put(vt, "\\x");
    CHECK(strcmp(vt->title, "second") == 0);
    CHECK(line_is(vt, 2, "      x")); // the text after the title is text

    put(vt, "\007");
    CHECK(vt->bell);
    // a DCS string is passed over
    put(vt, "\033P1$r0m\033\\y");
    CHECK(line_is(vt, 2, "      xy"));
    vt_free(vt);
}

static void test_charsets_and_modes(void)
{
    vt_t *vt = vt_new(10, 3, 0);
    put(vt, "\033(0lqk\033(Bq");
    CHECK_EQ(cell(vt, 0, 0)->ch, 0x250C);
    CHECK_EQ(cell(vt, 1, 0)->ch, 0x2500);
    CHECK_EQ(cell(vt, 2, 0)->ch, 0x2510);
    CHECK_EQ(cell(vt, 3, 0)->ch, 'q');

    put(vt, "\033[?25l\033[?1h\033[?2004h\033[5 q");
    CHECK(!vt->cursor_visible);
    CHECK(vt->app_cursor_keys);
    CHECK(vt->bracketed_paste);
    CHECK_EQ(vt->cursor_shape, VT_CURSOR_BAR);
    CHECK(vt->cursor_blink);
    CHECK(strcmp(vt_paste_begin(vt), "\033[200~") == 0);
    put(vt, "\033[!p"); // soft reset
    CHECK(vt->cursor_visible);
    CHECK(!vt->app_cursor_keys);
    CHECK(strcmp(vt_paste_end(vt), "") == 0);

    put(vt, "\033[2;1Ha\tb\033[3gc");
    CHECK_EQ(cell(vt, 8, 1)->ch, 'b');
    put(vt, "\r\t");
    CHECK_EQ(vt->cursor_x, 9); // no tab stops left
    vt_free(vt);
}

static void test_resize(void)
{
    vt_t *vt = vt_new(10, 4, 10);
    put(vt, "a\r\nb\r\nc\r\nd");
    vt_resize(vt, 6, 2);
    CHECK(line_is(vt, 0, "c"));
    CHECK(line_is(vt, 1, "d"));
    CHECK_EQ(vt->cursor_y, 1);
    CHECK_EQ(vt_scrollback_lines(vt), 2);
    CHECK(line_is(vt, -1, "b"));
    vt_resize(vt, 12, 5);
    CHECK(line_is(vt, 0, "c"));
    CHECK_EQ(vt->cols, 12);
    put(vt, "\033[1;12HZ");
    CHECK_EQ(cell(vt, 11, 0)->ch, 'Z');
    vt_free(vt);
}

static void test_keys(void)
{
    vt_t *vt = vt_new(10, 3, 0);
    char out[32];
    int n;
#define KEY_IS(expr, text) (n = (expr), out[n < 0 ? 0 : n] = '\0', strcmp(out, text) == 0)
    CHECK(KEY_IS(vt_key(vt, VT_KEY_UP, 0, out), "\033[A"));
    CHECK(KEY_IS(vt_key(vt, VT_KEY_LEFT, VT_MOD_CTRL, out), "\033[1;5D"));
    CHECK(KEY_IS(vt_key(vt, VT_KEY_DELETE, 0, out), "\033[3~"));
    CHECK(KEY_IS(vt_key(vt, VT_KEY_PAGE_UP, VT_MOD_SHIFT, out), "\033[5;2~"));
    CHECK(KEY_IS(vt_key(vt, VT_KEY_F1, 0, out), "\033OP"));
    CHECK(KEY_IS(vt_key(vt, VT_KEY_F1 + 4, 0, out), "\033[15~"));
    CHECK(KEY_IS(vt_key(vt, VT_KEY_F12, VT_MOD_ALT, out), "\033[24;3~"));
    CHECK(KEY_IS(vt_key(vt, VT_KEY_TAB, VT_MOD_SHIFT, out), "\033[Z"));
    CHECK(KEY_IS(vt_key(vt, VT_KEY_BACKSPACE, 0, out), "\x7F"));
    CHECK(KEY_IS(vt_key(vt, VT_KEY_ENTER, 0, out), "\r"));
    put(vt, "\033[?1h");
    CHECK(KEY_IS(vt_key(vt, VT_KEY_UP, 0, out), "\033OA"));
    CHECK(KEY_IS(vt_key(vt, VT_KEY_HOME, 0, out), "\033OH"));

    CHECK(KEY_IS(vt_text_key("c", VT_MOD_CTRL, out), "\x03"));
    CHECK(KEY_IS(vt_text_key("C", VT_MOD_CTRL, out), "\x03"));
    CHECK(KEY_IS(vt_text_key("[", VT_MOD_CTRL, out), "\033"));
    CHECK(KEY_IS(vt_text_key(" ", VT_MOD_CTRL, out), ""));
    CHECK_EQ(n, 1); // a NUL byte
    CHECK(KEY_IS(vt_text_key("x", VT_MOD_ALT, out), "\033x"));
    CHECK(KEY_IS(vt_text_key("\xC3\xA6", 0, out), "\xC3\xA6"));
#undef KEY_IS
    vt_free(vt);
}

static void test_mouse(void)
{
    vt_t *vt = vt_new(10, 3, 0);
    char out[32];
    CHECK_EQ(vt_mouse(vt, VT_MOUSE_PRESS, 0, 2, 1, 0, out), 0);
    put(vt, "\033[?1000h\033[?1006h");
    int n = vt_mouse(vt, VT_MOUSE_PRESS, 0, 2, 1, 0, out);
    out[n] = '\0';
    CHECK(strcmp(out, "\033[<0;3;2M") == 0);
    n = vt_mouse(vt, VT_MOUSE_RELEASE, 0, 2, 1, VT_MOD_CTRL, out);
    out[n] = '\0';
    CHECK(strcmp(out, "\033[<16;3;2m") == 0);
    CHECK_EQ(vt_mouse(vt, VT_MOUSE_MOVE, 0, 3, 1, 0, out), 0); // ?1000 has no motion
    n = vt_mouse(vt, VT_MOUSE_WHEEL_UP, -1, 0, 0, 0, out);
    out[n] = '\0';
    CHECK(strcmp(out, "\033[<64;1;1M") == 0);
    put(vt, "\033[?1006l\033[?1002h");
    n = vt_mouse(vt, VT_MOUSE_MOVE, 0, 3, 1, 0, out);
    CHECK_EQ(n, 6);
    CHECK(memcmp(out, "\033[M", 3) == 0);
    CHECK_EQ(out[3], 32 + 32);
    CHECK_EQ(out[4], 32 + 4);
    CHECK_EQ(out[5], 32 + 2);
    vt_free(vt);
}

/** random bytes, with many ESC and [: nothing breaks, the cursor stays in */
static void test_noise(void)
{
    vt_t *vt = vt_new(13, 7, 20);
    vt_set_reply(vt, on_reply, NULL);
    uint32_t seed = 12345;
    bool inside = true;
    for (int round = 0; round < 20000; round++)
    {
        uint8_t bytes[64];
        for (int i = 0; i < 64; i++)
        {
            seed = seed * 1103515245 + 12345;
            uint8_t b = (uint8_t)(seed >> 16);
            if (b % 7 == 0)
                b = 0x1B;
            else if (b % 11 == 0)
                b = '[';
            else if (b % 13 == 0)
                b = (uint8_t)('0' + b % 10);
            bytes[i] = b;
        }
        vt_write(vt, bytes, sizeof(bytes));
        replies[0] = '\0';
        if (round % 1000 == 999)
            vt_resize(vt, 5 + round % 17, 3 + round % 9);
        inside = inside && vt->cursor_x >= 0 && vt->cursor_x < vt->cols && vt->cursor_y >= 0 &&
                 vt->cursor_y < vt->rows;
    }
    CHECK(inside);
    vt_free(vt);
}

int main(void)
{
    test_text_and_wrap();
    test_cursor_and_erase();
    test_insert_delete();
    test_colors();
    test_scrollback();
    test_scroll_region();
    test_alt_screen();
    test_utf8();
    test_replies_and_title();
    test_charsets_and_modes();
    test_resize();
    test_keys();
    test_mouse();
    test_noise();
    return hosttest_done("vt");
}
