#include "console.h"
#include "fb_console.h"

#include "kernel/tty/tty.h"
#include "kernel/device/device.h"
#include "drivers/video/vga/vga.h"
#include "drivers/video/fb/fb.h"
#include "drivers/keyboard/keyboard.h"
#include "common/string.h"

/**
 * @file console.c
 * @brief the system console: a tty over either the framebuffer or VGA text.
 *
 * console_* is a thin facade. output bytes go through a global tty_t (the
 * terminal state machine in kernel/tty/tty.c), which draws onto whichever
 * backend was selected at init: the framebuffer (JetBrains Mono) when the
 * bootloader set a graphics mode, or the legacy VGA text buffer otherwise.
 * every existing caller (kernel printf, klog, the write syscall) is unchanged.
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

void console_initialize(const boot_info_t *boot)
{
    const tty_backend_t *backend;

    (void)boot; // fb_init already consumed it; presence is queried via fb_available.

    if (fb_available())
    {
        // graphics mode: render text on the framebuffer with JetBrains Mono.
        backend = fb_console_backend();
    }
    else
    {
        // no framebuffer: fall back to the VGA text buffer.
        vga_initialize();
        backend = &vga_backend;
    }

    tty_init(&console_tty, backend);

    // announce the console to the device registry.
    static device_t console_device;
    strncpy(console_device.name, "tty0", DEVICE_NAME_MAX - 1);
    console_device.name[DEVICE_NAME_MAX - 1] = '\0';
    console_device.type = DEVICE_TYPE_CHAR;
    console_device.ops = 0;
    console_device.driver_data = &console_tty;
    device_register(&console_device);
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
