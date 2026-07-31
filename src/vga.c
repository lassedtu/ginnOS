#include "vga.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_COLOR 0x07

static volatile unsigned short *const video_memory = (volatile unsigned short *)0xB8000;
static unsigned int cursor_x = 0;
static unsigned int cursor_y = 0;

static void vga_update_cursor(void)
{
    unsigned short cursor_position = (unsigned short)(cursor_y * VGA_WIDTH + cursor_x);

    __asm__ volatile("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
    __asm__ volatile("outb %0, %1" : : "a"((unsigned char)(cursor_position & 0xFF)), "Nd"((unsigned short)0x3D5));
    __asm__ volatile("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
    __asm__ volatile("outb %0, %1" : : "a"((unsigned char)((cursor_position >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
}

static void vga_scroll(void)
{
    unsigned short blank_cell = (unsigned short)' ' | ((unsigned short)VGA_COLOR << 8);

    for (unsigned int row = 1; row < VGA_HEIGHT; row++)
    {
        for (unsigned int column = 0; column < VGA_WIDTH; column++)
        {
            video_memory[(row - 1) * VGA_WIDTH + column] = video_memory[row * VGA_WIDTH + column];
        }
    }

    for (unsigned int column = 0; column < VGA_WIDTH; column++)
    {
        video_memory[(VGA_HEIGHT - 1) * VGA_WIDTH + column] = blank_cell;
    }

    if (cursor_y > 0)
    {
        cursor_y--;
    }
}

static void vga_new_line(void)
{
    cursor_x = 0;
    cursor_y++;

    if (cursor_y >= VGA_HEIGHT)
    {
        vga_scroll();
    }
}

void vga_clear(void)
{
    unsigned short blank_cell = (unsigned short)' ' | ((unsigned short)VGA_COLOR << 8);

    for (unsigned int index = 0; index < VGA_WIDTH * VGA_HEIGHT; index++)
    {
        video_memory[index] = blank_cell;
    }

    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor();
}

void vga_backspace(void)
{
    if (cursor_x == 0 && cursor_y == 0)
    {
        return;
    }

    if (cursor_x == 0)
    {
        cursor_y--;
        cursor_x = VGA_WIDTH - 1;
    }
    else
    {
        cursor_x--;
    }

    video_memory[cursor_y * VGA_WIDTH + cursor_x] = (unsigned short)' ' | ((unsigned short)VGA_COLOR << 8);
    vga_update_cursor();
}

void vga_write_char(char character)
{
    if (character == '\n')
    {
        vga_new_line();
        vga_update_cursor();
        return;
    }

    video_memory[cursor_y * VGA_WIDTH + cursor_x] = (unsigned short)character | ((unsigned short)VGA_COLOR << 8);
    cursor_x++;

    if (cursor_x >= VGA_WIDTH)
    {
        vga_new_line();
    }

    vga_update_cursor();
}

void vga_write_line(const char *text)
{
    vga_write_string(text);
    vga_write_char('\n');
}

void vga_write_string(const char *text)
{
    for (unsigned int index = 0; text[index] != '\0'; index++)
    {
        vga_write_char(text[index]);
    }
}