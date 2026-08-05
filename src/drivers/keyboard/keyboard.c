#include "keyboard.h"

#include "../../arch/x86/cpu/irq.h"
#include "../../arch/x86/cpu/isr.h"
#include "../../arch/x86/cpu/io.h"
#include "../../common/stdint.h"
#include "../../common/stdio.h"

/* This file is just a simple version of a keyboard driver
    to check that everything works.
*/

/**
 * keyboard scancode to ASCII character mapping for standard US QWERTY layout.
 * only handles key presses (ignores releases) and does not handle modifier keys.
 */
static const char keyboard_map[128] =
    {
        0,
        27,
        '1',
        '2',
        '3',
        '4',
        '5',
        '6',
        '7',
        '8',
        '9',
        '0',
        '-',
        '=',
        '\b',
        '\t',
        'q',
        'w',
        'e',
        'r',
        't',
        'y',
        'u',
        'i',
        'o',
        'p',
        '[',
        ']',
        '\n',
        0,
        'a',
        's',
        'd',
        'f',
        'g',
        'h',
        'j',
        'k',
        'l',
        ';',
        '\'',
        '`',
        0,
        '\\',
        'z',
        'x',
        'c',
        'v',
        'b',
        'n',
        'm',
        ',',
        '.',
        '/',
        [127] = 0};

static void keyboard_handler(struct registers *regs)
{
    (void)regs;

    uint8_t scancode = io_inb(0x60);

    // ignore key releases
    if (scancode & 0x80)
        return;

    if (scancode < 128)
    {
        char c = keyboard_map[scancode];

        if (c)
        {
            printf("Key pressed: %c\r\n", c);
        }
    }
}

void keyboard_initialize(void)
{
    irq_register_handler(1, keyboard_handler);
}