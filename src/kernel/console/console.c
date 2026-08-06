#include "console.h"

#include "../../drivers/video/vga/vga.h"

void console_initialize(void)
{
    vga_initialize();
}

void console_putchar(char c)
{
    uint8_t row;
    uint8_t col;

    switch (c)
    {
    case '\n':
        vga_get_cursor(&row, &col);

        vga_set_cursor(
            row + 1,
            0);
        break;

    case '\r':
        vga_get_cursor(&row, &col);

        vga_set_cursor(
            row,
            0);
        break;

    case '\b':
        vga_get_cursor(&row, &col);

        if (col > 0)
        {
            col--;

            vga_set_cursor(row, col);
            vga_put_at(' ', row, col);
            vga_set_cursor(row, col);
        }

        break;

    default:
        vga_putchar(c);
        break;
    }
}

void console_write(const char *str)
{
    while (*str)
    {
        console_putchar(*str++);
    }
}