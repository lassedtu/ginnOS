/**
 * @file bragi_cell.c
 * @brief bragi: the monospace grid, with box drawing done in code.
 *
 * a font's box drawing glyphs do not always fill the cell (the cell is
 * rounded to whole pixels), so lines in a terminal get gaps. the single
 * and heavy lines (U+2500 to U+254F, U+2574 to U+257F, and the round
 * corners) and the block elements (U+2580 to U+259F) are drawn here as
 * rectangles that fill the cell. the double lines and the diagonals come
 * from the font.
 */

#include "bragi_internal.h"

// the arms of a line character, 2 bits each: 0 none, 1 light, 2 heavy
#define ARMS(up, right, down, left) (uint8_t)((up) << 6 | (right) << 4 | (down) << 2 | (left))
#define ARM_UP(a) (((a) >> 6) & 3)
#define ARM_RIGHT(a) (((a) >> 4) & 3)
#define ARM_DOWN(a) (((a) >> 2) & 3)
#define ARM_LEFT(a) ((a) & 3)

// U+2500 to U+254F (the dashed lines are drawn solid)
static const uint8_t lines[0x50] = {
    ARMS(0, 1, 0, 1), ARMS(0, 2, 0, 2), ARMS(1, 0, 1, 0), ARMS(2, 0, 2, 0), // 2500
    ARMS(0, 1, 0, 1), ARMS(0, 2, 0, 2), ARMS(1, 0, 1, 0), ARMS(2, 0, 2, 0), // 2504
    ARMS(0, 1, 0, 1), ARMS(0, 2, 0, 2), ARMS(1, 0, 1, 0), ARMS(2, 0, 2, 0), // 2508
    ARMS(0, 1, 1, 0), ARMS(0, 2, 1, 0), ARMS(0, 1, 2, 0), ARMS(0, 2, 2, 0), // 250C
    ARMS(0, 0, 1, 1), ARMS(0, 0, 1, 2), ARMS(0, 0, 2, 1), ARMS(0, 0, 2, 2), // 2510
    ARMS(1, 1, 0, 0), ARMS(1, 2, 0, 0), ARMS(2, 1, 0, 0), ARMS(2, 2, 0, 0), // 2514
    ARMS(1, 0, 0, 1), ARMS(1, 0, 0, 2), ARMS(2, 0, 0, 1), ARMS(2, 0, 0, 2), // 2518
    ARMS(1, 1, 1, 0), ARMS(1, 2, 1, 0), ARMS(2, 1, 1, 0), ARMS(1, 1, 2, 0), // 251C
    ARMS(2, 1, 2, 0), ARMS(2, 2, 1, 0), ARMS(1, 2, 2, 0), ARMS(2, 2, 2, 0), // 2520
    ARMS(1, 0, 1, 1), ARMS(1, 0, 1, 2), ARMS(2, 0, 1, 1), ARMS(1, 0, 2, 1), // 2524
    ARMS(2, 0, 2, 1), ARMS(2, 0, 1, 2), ARMS(1, 0, 2, 2), ARMS(2, 0, 2, 2), // 2528
    ARMS(0, 1, 1, 1), ARMS(0, 1, 1, 2), ARMS(0, 2, 1, 1), ARMS(0, 2, 1, 2), // 252C
    ARMS(0, 1, 2, 1), ARMS(0, 1, 2, 2), ARMS(0, 2, 2, 1), ARMS(0, 2, 2, 2), // 2530
    ARMS(1, 1, 0, 1), ARMS(1, 1, 0, 2), ARMS(1, 2, 0, 1), ARMS(1, 2, 0, 2), // 2534
    ARMS(2, 1, 0, 1), ARMS(2, 1, 0, 2), ARMS(2, 2, 0, 1), ARMS(2, 2, 0, 2), // 2538
    ARMS(1, 1, 1, 1), ARMS(1, 1, 1, 2), ARMS(1, 2, 1, 1), ARMS(1, 2, 1, 2), // 253C
    ARMS(2, 1, 1, 1), ARMS(1, 1, 2, 1), ARMS(2, 1, 2, 1), ARMS(2, 1, 1, 2), // 2540
    ARMS(2, 2, 1, 1), ARMS(1, 1, 2, 2), ARMS(1, 2, 2, 1), ARMS(2, 2, 1, 2), // 2544
    ARMS(1, 2, 2, 2), ARMS(2, 1, 2, 2), ARMS(2, 2, 2, 1), ARMS(2, 2, 2, 2), // 2548
    ARMS(0, 1, 0, 1), ARMS(0, 2, 0, 2), ARMS(1, 0, 1, 0), ARMS(2, 0, 2, 0), // 254C
};

// U+256D to U+257F (the diagonals U+2571 to U+2573 are 0: from the font)
static const uint8_t more_lines[0x13] = {
    ARMS(0, 1, 1, 0), ARMS(0, 0, 1, 1), ARMS(1, 0, 0, 1), ARMS(1, 1, 0, 0), // 256D round
    0, 0, 0,                                                                // 2571
    ARMS(0, 0, 0, 1), ARMS(1, 0, 0, 0), ARMS(0, 1, 0, 0), ARMS(0, 0, 1, 0), // 2574
    ARMS(0, 0, 0, 2), ARMS(2, 0, 0, 0), ARMS(0, 2, 0, 0), ARMS(0, 0, 2, 0), // 2578
    ARMS(0, 2, 0, 1), ARMS(1, 0, 2, 0), ARMS(0, 1, 0, 2), ARMS(2, 0, 1, 0), // 257C
};

void bragi_cell_size(bragi_face_t *face, int32_t *width, int32_t *height)
{
    int glyph;
    bragi_face_t *f = bragi_find_glyph(face, 'M', &glyph);
    *width = (bragi_advance(f, glyph) + BRAGI_UNITS / 2) / BRAGI_UNITS;
    *height = face->line_height;
}

static int32_t thickness(int32_t weight, int32_t light)
{
    return weight == 0 ? 0 : weight == 1 ? light : light * 2;
}

static int32_t max32(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

/**
 * draw the arms of a line character in a cell.
 */
static void draw_lines(gfx_buffer_t *buf, gfx_rect_t cell, uint8_t arms, gfx_color_t color)
{
    int32_t light = max32(1, (cell.w + 4) / 9);
    int32_t up = thickness(ARM_UP(arms), light);
    int32_t right = thickness(ARM_RIGHT(arms), light);
    int32_t down = thickness(ARM_DOWN(arms), light);
    int32_t left = thickness(ARM_LEFT(arms), light);
    int32_t mx = cell.x + cell.w / 2;
    int32_t my = cell.y + cell.h / 2;

    // the thickest line across each axis sets how far the other arms reach
    int32_t vertical = max32(up, down);
    int32_t horizontal = max32(left, right);

    if (left)
    {
        int32_t reach = vertical ? mx - vertical / 2 + vertical : mx - left / 2 + left;
        gfx_fill_rect(buf, gfx_rect(cell.x, my - left / 2, reach - cell.x, left), color);
    }
    if (right)
    {
        int32_t from = vertical ? mx - vertical / 2 : mx - right / 2;
        gfx_fill_rect(buf, gfx_rect(from, my - right / 2, cell.x + cell.w - from, right), color);
    }
    if (up)
    {
        int32_t reach = horizontal ? my - horizontal / 2 : my - up / 2 + up;
        if (horizontal && !left && !right)
            reach = my;
        gfx_fill_rect(buf, gfx_rect(mx - up / 2, cell.y, up, reach - cell.y), color);
    }
    if (down)
    {
        int32_t from = horizontal ? my - horizontal / 2 + horizontal : my - down / 2;
        gfx_fill_rect(buf, gfx_rect(mx - down / 2, from, down, cell.y + cell.h - from), color);
    }
}

/**
 * draw a block element (U+2580 to U+259F).
 */
static void draw_block(gfx_buffer_t *buf, gfx_rect_t cell, uint32_t c, gfx_color_t color)
{
    int32_t x = cell.x;
    int32_t y = cell.y;
    int32_t w = cell.w;
    int32_t h = cell.h;
    int32_t hw = w / 2;
    int32_t hh = h / 2;
    if (c == 0x2580)
        gfx_fill_rect(buf, gfx_rect(x, y, w, hh), color);
    else if (c >= 0x2581 && c <= 0x2588)
    {
        int32_t part = h * (int32_t)(c - 0x2580) / 8;
        gfx_fill_rect(buf, gfx_rect(x, y + h - part, w, part), color);
    }
    else if (c >= 0x2589 && c <= 0x258F)
    {
        int32_t part = w * (int32_t)(0x2590 - c) / 8;
        gfx_fill_rect(buf, gfx_rect(x, y, part, h), color);
    }
    else if (c == 0x2590)
        gfx_fill_rect(buf, gfx_rect(x + hw, y, w - hw, h), color);
    else if (c >= 0x2591 && c <= 0x2593)
        gfx_fill_rect(buf, cell, gfx_fade(color, (uint8_t)(64 * (c - 0x2590))));
    else if (c == 0x2594)
        gfx_fill_rect(buf, gfx_rect(x, y, w, max32(1, h / 8)), color);
    else if (c == 0x2595)
        gfx_fill_rect(buf, gfx_rect(x + w - max32(1, w / 8), y, max32(1, w / 8), h), color);
    else
    {
        // the quadrants, U+2596 to U+259F: bits for top left, top right,
        // bottom left and bottom right
        static const uint8_t quads[10] = {4, 8, 1, 13, 9, 7, 11, 2, 6, 14};
        uint8_t q = quads[c - 0x2596];
        if (q & 1)
            gfx_fill_rect(buf, gfx_rect(x, y, hw, hh), color);
        if (q & 2)
            gfx_fill_rect(buf, gfx_rect(x + hw, y, w - hw, hh), color);
        if (q & 4)
            gfx_fill_rect(buf, gfx_rect(x, y + hh, hw, h - hh), color);
        if (q & 8)
            gfx_fill_rect(buf, gfx_rect(x + hw, y + hh, w - hw, h - hh), color);
    }
}

void bragi_draw_cell(gfx_buffer_t *buf, bragi_face_t *face, int32_t x, int32_t y,
                     uint32_t codepoint, gfx_color_t color)
{
    int32_t cw;
    int32_t ch;
    bragi_cell_size(face, &cw, &ch);
    gfx_rect_t cell = gfx_rect(x, y, cw, ch);

    uint8_t arms = 0;
    if (codepoint >= 0x2500 && codepoint <= 0x254F)
        arms = lines[codepoint - 0x2500];
    else if (codepoint >= 0x256D && codepoint <= 0x257F)
        arms = more_lines[codepoint - 0x256D];
    if (arms)
    {
        draw_lines(buf, cell, arms, color);
        return;
    }
    if (codepoint >= 0x2580 && codepoint <= 0x259F)
    {
        draw_block(buf, cell, codepoint, color);
        return;
    }

    char utf8[4];
    int32_t n;
    if (codepoint < 0x80)
    {
        utf8[0] = (char)codepoint;
        n = 1;
    }
    else if (codepoint < 0x800)
    {
        utf8[0] = (char)(0xC0 | codepoint >> 6);
        utf8[1] = (char)(0x80 | (codepoint & 0x3F));
        n = 2;
    }
    else if (codepoint < 0x10000)
    {
        utf8[0] = (char)(0xE0 | codepoint >> 12);
        utf8[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (codepoint & 0x3F));
        n = 3;
    }
    else
    {
        utf8[0] = (char)(0xF0 | codepoint >> 18);
        utf8[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        utf8[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[3] = (char)(0x80 | (codepoint & 0x3F));
        n = 4;
    }
    bragi_draw(buf, face, x, y + face->ascent, utf8, n, color);
}
