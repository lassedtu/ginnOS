#include "console.h"

#include "kernel/tty/tty.h"
#include "drivers/video/vga/vga.h"
#include "drivers/keyboard/keyboard.h"

/**
 * @file console.c
 * @brief the system console: a VGA-backed tty plus cooked-mode line input.
 *
 * console_* is now a thin facade. output bytes go through a global tty_t
 * (the terminal state machine in kernel/tty/tty.c), which draws onto the VGA
 * text buffer via the backend ops below. keeping this facade means every
 * existing caller (kernel printf, klog, the write syscall) is unchanged.
 */

#define CONSOLE_VGA_WIDTH 80u
#define CONSOLE_VGA_HEIGHT 25u

// VGA character-cell backend handed to the tty. these just forward to the
// raw framebuffer driver, which knows nothing about ANSI or line discipline.
static const tty_backend_t vga_backend = {
    .put_at = vga_put_at,
    .scroll = vga_scroll,
    .clear = vga_clear,
    .set_cursor = vga_set_cursor,
    .get_cursor = vga_get_cursor,
    .rows = CONSOLE_VGA_HEIGHT,
    .cols = CONSOLE_VGA_WIDTH,
};

static tty_t console_tty;

void console_initialize(void)
{
    vga_initialize();
    tty_init(&console_tty, &vga_backend);
}

void console_putchar(char c)
{
    tty_putchar(&console_tty, c);
}

void console_write(const char *str)
{
    tty_write(&console_tty, str);
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
