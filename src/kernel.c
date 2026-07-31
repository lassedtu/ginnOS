#include "kernel.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_COLOR 0x07

static volatile unsigned short *const video_memory = (volatile unsigned short *)0xB8000;

static void clear_screen(void)
{
    unsigned short blank_cell = (unsigned short)' ' | ((unsigned short)VGA_COLOR << 8);

    for (unsigned int index = 0; index < VGA_WIDTH * VGA_HEIGHT; index++)
    {
        video_memory[index] = blank_cell;
    }
}

static void write_string(const char *text)
{
    for (unsigned int index = 0; text[index] != '\0'; index++)
    {
        video_memory[index] = (unsigned short)text[index] | ((unsigned short)VGA_COLOR << 8);
    }
}

void kernel_main()
{
    clear_screen();
    write_string("Ginnung booted!");
}