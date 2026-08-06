#include "console.h"

#include "../../drivers/video/vga/vga.h"

void console_initialize(void)
{
    vga_initialize();
}

void console_putchar(char c)
{
    vga_putchar(c);
}

void console_write(const char *str)
{
    while (*str)
    {
        console_putchar(*str++);
    }
}