#include "../common/stdint.h"

#define VGA_TEXT_BUFFER ((volatile uint16_t *)0xB8000)

#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u
#define VGA_ATTR 0x07u

#define VGA_CRTC_INDEX_PORT 0x3D4u
#define VGA_CRTC_DATA_PORT 0x3D5u

static uint32_t g_cursor_row = 0;
static uint32_t g_cursor_col = 0;
static uint8_t g_cursor_synced = 0;

/**
 * write a byte to an I/O port.
 */
static inline void io_outb(uint16_t port, uint8_t value)
{
    __asm__ __volatile__(
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port));
}

/**
 * read a byte from an I/O port.
 */
static inline uint8_t io_inb(uint16_t port)
{
    uint8_t value;

    __asm__ __volatile__(
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port));

    return value;
}

/**
 * update the hardware cursor position to match the current cursor row and column.
 */
static void vga_update_hw_cursor(void)
{
    uint16_t position;

    if (g_cursor_row >= VGA_HEIGHT)
    {
        g_cursor_row = VGA_HEIGHT - 1u;
    }

    if (g_cursor_col >= VGA_WIDTH)
    {
        g_cursor_col = VGA_WIDTH - 1u;
    }

    position = (uint16_t)(g_cursor_row * VGA_WIDTH + g_cursor_col);

    io_outb(VGA_CRTC_INDEX_PORT, 0x0E);
    io_outb(
        VGA_CRTC_DATA_PORT,
        (uint8_t)(position >> 8));

    io_outb(VGA_CRTC_INDEX_PORT, 0x0F);
    io_outb(
        VGA_CRTC_DATA_PORT,
        (uint8_t)(position & 0xFFu));
}

static void vga_sync_cursor_from_hw_once(void)
{
    uint16_t position;

    if (g_cursor_synced)
    {
        return;
    }

    io_outb(VGA_CRTC_INDEX_PORT, 0x0E);

    position =
        (uint16_t)io_inb(VGA_CRTC_DATA_PORT) << 8;

    io_outb(VGA_CRTC_INDEX_PORT, 0x0F);

    position |= io_inb(VGA_CRTC_DATA_PORT);

    if (position >= VGA_WIDTH * VGA_HEIGHT)
    {
        position = 0;
    }

    g_cursor_row = position / VGA_WIDTH;
    g_cursor_col = position % VGA_WIDTH;

    g_cursor_synced = 1;
}

static void vga_scroll(void)
{
    uint32_t row;
    uint32_t col;

    for (row = 0; row < VGA_HEIGHT - 1u; row++)
    {
        for (col = 0; col < VGA_WIDTH; col++)
        {
            VGA_TEXT_BUFFER[row * VGA_WIDTH + col] =
                VGA_TEXT_BUFFER[(row + 1u) * VGA_WIDTH + col];
        }
    }

    for (col = 0; col < VGA_WIDTH; col++)
    {
        VGA_TEXT_BUFFER[(VGA_HEIGHT - 1u) * VGA_WIDTH + col] =
            ((uint16_t)VGA_ATTR << 8) | ' ';
    }
}

static void vga_scroll_if_needed(void)
{
    if (g_cursor_row < VGA_HEIGHT)
    {
        return;
    }

    vga_scroll();

    g_cursor_row = VGA_HEIGHT - 1u;
}

void puts_char(char c)
{
    vga_sync_cursor_from_hw_once();

    if (c == '\r')
    {
        g_cursor_col = 0;
    }
    else if (c == '\n')
    {
        g_cursor_col = 0;
        g_cursor_row++;
    }
    else
    {
        VGA_TEXT_BUFFER[g_cursor_row * VGA_WIDTH + g_cursor_col] =
            ((uint16_t)VGA_ATTR << 8) | (uint8_t)c;

        g_cursor_col++;

        if (g_cursor_col >= VGA_WIDTH)
        {
            g_cursor_col = 0;
            g_cursor_row++;
        }
    }

    vga_scroll_if_needed();

    vga_update_hw_cursor();
}