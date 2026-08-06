#pragma once

#include "../../../common/stdint.h"

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
 * writes a single character to the VGA text buffer at the current cursor position.
 * handles special characters like newline, carriage return, and backspace.
 * @param c the character to write.
 */
void vga_putchar(char c);

/**
 * writes a null-terminated string to the VGA text buffer starting at the current cursor position.
 * @param str the string to write.
 */
void vga_write(const char *str);