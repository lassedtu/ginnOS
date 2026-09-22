#pragma once

/**
 * @file fb_text.h
 * @brief text grid on top of the linear framebuffer.
 *
 * turns the pixel-only framebuffer driver into a character grid: it lays out
 * cells using the generated bitmap font (font.h), draws glyphs with a
 * foreground/background colour, tracks a cursor, and scrolls. it does not
 * parse escape sequences, that is the tty layer's job. this is the piece the
 * framebuffer tty backend calls.
 */

#include "common/stdint.h"

/**
 * initialize the text grid over the (already initialized) framebuffer.
 * computes the cell size from the font plus scaling/margins, derives the
 * rows/cols that fit, and clears the screen.
 * @param scale integer glyph magnification (1 = native font size).
 * @param margin pixels of blank border on each edge of the text area.
 */
void fb_text_init(uint32_t scale, uint32_t margin);

/**
 * number of character rows / columns that fit on screen.
 */
uint8_t fb_text_rows(void);
uint8_t fb_text_cols(void);

/**
 * draw a glyph at a cell with explicit 0x00RRGGBB colours.
 * @param row, col target cell (out-of-range is ignored).
 * @param ch character code (0-255).
 * @param fg foreground colour, @param bg background colour.
 */
void fb_text_draw_glyph(uint8_t row, uint8_t col, char ch, uint32_t fg, uint32_t bg);

/**
 * set the active foreground/background as ANSI colour indices (0-15). glyphs
 * drawn via fb_text_put use these until changed.
 */
void fb_text_set_colors(uint8_t fg, uint8_t bg);

/**
 * draw a glyph at a cell using the active ANSI colours (set via
 * fb_text_set_colors). this is what the tty backend calls.
 */
void fb_text_put(uint8_t row, uint8_t col, char ch);

/**
 * draw or erase the software caret at a cell. when erasing, the cell is
 * repainted with the active background so the underlying glyph is preserved
 * by a subsequent redraw. drawn as an underline near the cell bottom.
 */
void fb_text_draw_cursor(uint8_t row, uint8_t col, bool visible);

/**
 * clear the whole grid to the current background colour.
 */
void fb_text_clear(void);

/**
 * scroll the grid up by one row; the new bottom row is cleared.
 */
void fb_text_scroll(void);

/**
 * move / query the logical cursor (used by the tty backend to draw a caret).
 */
void fb_text_set_cursor(uint8_t row, uint8_t col);
void fb_text_get_cursor(uint8_t *row, uint8_t *col);

/**
 * set the default background used by clear/scroll (0x00RRGGBB).
 */
void fb_text_set_default_bg(uint32_t bg);
