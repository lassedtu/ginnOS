#pragma once

#include "common/stdint.h"

/**
 * enumeration of VGA colors.
 */
enum vga_color
{
    VGA_BLACK = 0,
    VGA_BLUE,
    VGA_GREEN,
    VGA_CYAN,
    VGA_RED,
    VGA_MAGENTA,
    VGA_BROWN,
    VGA_LIGHT_GREY,
    VGA_DARK_GREY,
    VGA_LIGHT_BLUE,
    VGA_LIGHT_GREEN,
    VGA_LIGHT_CYAN,
    VGA_LIGHT_RED,
    VGA_LIGHT_MAGENTA,
    VGA_YELLOW,
    VGA_WHITE
};

/**
 * initializes the VGA driver. must be called before any other VGA functions.
 */
void vga_initialize(void);

/**
 * clears the VGA text buffer and resets the cursor position to the top left corner.
 */
void vga_clear(void);

/**
 * sets the foreground and background colors for text output.
 * @param foreground the foreground color.
 * @param background the background color.
 */
void vga_set_color(
    uint8_t foreground,
    uint8_t background);

/**
 * write a character at an explicit screen location.
 * invalid row/column values are ignored.
 */
void vga_put_at(char c, uint8_t row, uint8_t col);

/**
 * scroll the text buffer up by one row and move the cursor to the last row.
 */
void vga_scroll(void);

/**
 * set the cursor position to the specified row and column.
 * @param row the row to set the cursor to.
 * @param col the column to set the cursor to.
 */
void vga_set_cursor(uint8_t row, uint8_t col);

/**
 * get the current cursor position.
 * @param row pointer to a variable to receive the current cursor row.
 * @param col pointer to a variable to receive the current cursor column.
 */
void vga_get_cursor(uint8_t *row, uint8_t *col);

/**
 * writes a single character to the VGA text buffer at the current cursor position.
 * @param c the character to write.
 */
void vga_putchar(char c);

/**
 * writes a null-terminated string to the VGA text buffer starting at the current cursor position.
 * @param str the string to write.
 */
void vga_write(const char *str);