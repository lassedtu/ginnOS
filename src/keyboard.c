#include "keyboard.h"

#include "shell.h"
#include "vga.h"

static unsigned char keyboard_read_scancode(void)
{
    unsigned char scancode;

    __asm__ volatile("inb %1, %0" : "=a"(scancode) : "Nd"((unsigned short)0x60));
    return scancode;
}

static char keyboard_translate_scancode(unsigned char scancode)
{
    static const char lookup_table[128] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x',
        'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, 0, 0, ' '};

    if (scancode >= sizeof(lookup_table))
    {
        return 0;
    }

    return lookup_table[scancode];
}

void keyboard_handle_irq(void)
{
    unsigned char scancode = keyboard_read_scancode();

    if (scancode & 0x80)
    {
        return;
    }

    char character = keyboard_translate_scancode(scancode);

    if (character != 0)
    {
        shell_handle_char(character);
    }
}