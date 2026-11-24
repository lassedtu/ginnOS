#include "console.h"
#include "fb_console.h"

#include "kernel/tty/tty.h"
#include "kernel/device/device.h"
#include "kernel/sync/mutex.h"
#include "kernel/sync/spinlock.h"
#include "drivers/video/vga/vga.h"
#include "drivers/video/fb/fb.h"
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
 *
 * locking: the tty state (escape parser, cursor, colours, batch depth) and
 * the backend are guarded by console_lock, a spinlock taken around each
 * character, so kernel printf from any context (an IRQ handler included)
 * never sees a half-updated terminal, and interrupts stay masked for one
 * glyph (or one scroll) at a time, never a whole write. writes from
 * processes also hold write_mutex for their whole length, so two processes'
 * output (and their escape sequences) don't interleave.
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

// guards console_tty and the backend it draws on
static spinlock_t console_lock;

// held across a whole console_write_bytes (process context only)
static mutex_t write_mutex;

// set by console_panic_mode(): the CPU is halting, so locking is skipped
static volatile bool panic_mode;

/**
 * take console_lock, unless a panic is printing.
 * @return the interrupt state for console_unlock().
 */
static uint32_t console_lock_acquire(void)
{
    if (panic_mode)
    {
        return 0;
    }
    return spin_lock_irqsave(&console_lock);
}

/**
 * release console_lock taken by console_lock_acquire().
 * @param flags the value console_lock_acquire() returned.
 */
static void console_lock_release(uint32_t flags)
{
    if (panic_mode)
    {
        return;
    }
    spin_unlock_irqrestore(&console_lock, flags);
}

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

    spinlock_init(&console_lock);
    mutex_init(&write_mutex);
    tty_init(&console_tty, backend);

    // announce the console to the device registry.
    static device_t console_device;
    strncpy(console_device.name, "tty0", DEVICE_NAME_MAX - 1);
    console_device.name[DEVICE_NAME_MAX - 1] = '\0';
    console_device.type = DEVICE_TYPE_CHAR;
    console_device.number = CONSOLE_DEVICE_NUMBER;
    console_device.ops = 0;
    console_device.driver_data = &console_tty;
    device_register(&console_device);
}

void console_putchar(char c)
{
    uint32_t flags = console_lock_acquire();
    tty_putchar(&console_tty, c);
    console_lock_release(flags);
}

void console_write(const char *str)
{
    while (*str)
    {
        console_putchar(*str++);
    }
}

void console_write_bytes(const char *buf, uint32_t len)
{
    mutex_lock(&write_mutex);

    // one batch for the whole buffer: the caret is hidden once and the
    // display flushed once at the end. kernel printf landing in between
    // draws into the open batch and is flushed with it.
    uint32_t flags = console_lock_acquire();
    tty_begin_batch(&console_tty);
    console_lock_release(flags);

    for (uint32_t i = 0; i < len; i++)
    {
        flags = console_lock_acquire();
        // output newline translation: \n becomes \r\n on the terminal.
        if (buf[i] == '\n')
        {
            tty_putchar(&console_tty, '\r');
        }
        tty_putchar(&console_tty, buf[i]);
        console_lock_release(flags);
    }

    flags = console_lock_acquire();
    tty_end_batch(&console_tty);
    console_lock_release(flags);

    mutex_unlock(&write_mutex);
}

void console_panic_mode(void)
{
    panic_mode = true;
    // a panic is shown even when a program has the screen
    fb_release_graphics();
}

void console_size(uint32_t *rows, uint32_t *cols)
{
    *rows = console_tty.backend ? console_tty.backend->rows : CONSOLE_VGA_HEIGHT;
    *cols = console_tty.backend ? console_tty.backend->cols : CONSOLE_VGA_WIDTH;
}
