#include "console.h"

#include "../../drivers/video/vga/vga.h"
#include "../../drivers/keyboard/keyboard.h"

#define CONSOLE_VGA_HEIGHT 25u

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

        row++;

        if (row >= CONSOLE_VGA_HEIGHT)
        {
            vga_scroll();
            vga_get_cursor(&row, &col);
        }

        vga_set_cursor(row, 0);
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

int console_readline(char *buffer, int size)
{
    int length = 0;

    while (1)
    {
        while (!keyboard_available())
        {
        }

        char c = keyboard_getchar();

        if (c == '\n')
        {
            buffer[length] = 0;
            console_putchar('\n');
            return length;
        }

        if (c == '\b')
        {
            if (length > 0)
            {
                length--;
                console_putchar('\b');
            }

            continue;
        }

        if (length < size - 1)
        {
            buffer[length++] = c;
            console_putchar(c);
        }
    }
}