#include "vga.h"

#include "../../../arch/x86/cpu/io.h"

#define VGA_TEXT_BUFFER ((volatile uint16_t *)0xB8000)

#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u

#define VGA_DEFAULT_FOREGROUND VGA_LIGHT_GREY
#define VGA_DEFAULT_BACKGROUND VGA_BLACK

static uint8_t vga_color =
    VGA_DEFAULT_FOREGROUND |
    (VGA_DEFAULT_BACKGROUND << 4);

#define VGA_CRTC_INDEX_PORT 0x3D4u
#define VGA_CRTC_DATA_PORT 0x3D5u

static uint32_t g_cursor_row = 0;
static uint32_t g_cursor_col = 0;

static uint8_t initialized = 0;

static inline uint16_t vga_entry(char c)
{
    return ((uint16_t)vga_color << 8) | (uint8_t)c;
}

/**
 * update the hardware cursor position to match the current cursor row and column.
 */
static void vga_update_hw_cursor(void)
{
    uint16_t position;

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

/**
 * synchronize software cursor coordinates from the VGA hardware cursor.
 */
static void vga_sync_cursor_from_hw(void)
{
    uint16_t position;

    io_outb(VGA_CRTC_INDEX_PORT, 0x0E);
    position = (uint16_t)io_inb(VGA_CRTC_DATA_PORT) << 8;

    io_outb(VGA_CRTC_INDEX_PORT, 0x0F);
    position |= (uint16_t)io_inb(VGA_CRTC_DATA_PORT);

    if (position >= (VGA_WIDTH * VGA_HEIGHT))
    {
        position = 0;
    }

    g_cursor_row = position / VGA_WIDTH;
    g_cursor_col = position % VGA_WIDTH;
}

void vga_put_at(char c, uint8_t row, uint8_t col)
{
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH)
    {
        return;
    }

    VGA_TEXT_BUFFER[row * VGA_WIDTH + col] = vga_entry(c);
}

void vga_scroll(void)
{
    uint8_t row;
    uint8_t col;

    for (row = 0; row < VGA_HEIGHT - 1u; row++)
    {
        for (col = 0; col < VGA_WIDTH; col++)
        {
            char ch = (char)(VGA_TEXT_BUFFER[(row + 1u) * VGA_WIDTH + col] & 0x00FFu);
            vga_put_at(ch, row, col);
        }
    }

    for (col = 0; col < VGA_WIDTH; col++)
    {
        vga_put_at(' ', VGA_HEIGHT - 1u, col);
    }

    g_cursor_row = VGA_HEIGHT - 1u;
}

void vga_set_color(uint8_t fg, uint8_t bg)
{
    vga_color = fg | (bg << 4);
}

void vga_clear(void)
{
    for (uint8_t row = 0; row < VGA_HEIGHT; row++)
    {
        for (uint8_t col = 0; col < VGA_WIDTH; col++)
        {
            vga_put_at(' ', row, col);
        }
    }

    g_cursor_row = 0;
    g_cursor_col = 0;

    vga_update_hw_cursor();
}

void vga_putchar(char c)
{
    vga_put_at(c, g_cursor_row, g_cursor_col);

    g_cursor_col++;

    if (g_cursor_col >= VGA_WIDTH)
    {
        g_cursor_col = 0;
        g_cursor_row++;
    }

    if (g_cursor_row >= VGA_HEIGHT)
    {
        vga_scroll();
    }

    vga_update_hw_cursor();
}

void vga_write(const char *str)
{
    while (*str)
    {
        vga_putchar(*str++);
    }
}

void vga_initialize(void)
{
    if (initialized)
        return;

    vga_sync_cursor_from_hw();

    initialized = 1;
}

void vga_set_cursor(uint8_t row, uint8_t col)
{
    g_cursor_row = row;
    g_cursor_col = col;

    vga_update_hw_cursor();
}

void vga_get_cursor(uint8_t *row, uint8_t *col)
{
    *row = g_cursor_row;
    *col = g_cursor_col;
}