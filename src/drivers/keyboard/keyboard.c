#include "keyboard.h"

#include "../../arch/x86/cpu/irq.h"
#include "../../arch/x86/cpu/isr.h"
#include "../../arch/x86/cpu/io.h"
#include "../../arch/x86/cpu/pic.h"
#include "../../common/stdint.h"

#define KEYBOARD_BUFFER_SIZE 128 // size of the keyboard input buffer

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE]; // circular buffer for storing keyboard input

static uint16_t buffer_read = 0;  // index of the next character to read from the buffer
static uint16_t buffer_write = 0; // index of the next position to write a character to the buffer

static key_state_t shift_state = KEY_RELEASED; // state of the shift key (pressed or released)
static key_state_t ctrl_state = KEY_RELEASED;  // state of the control key (pressed or released)
static key_state_t alt_state = KEY_RELEASED;   // state of the alt key (pressed or released)

static int caps_lock_enabled = 0; // state of the caps lock key (enabled or disabled)

typedef enum
{
    SCANCODE_LEFT_SHIFT = 0x2A,
    SCANCODE_RIGHT_SHIFT = 0x36,
    SCANCODE_CAPS_LOCK = 0x3A,
    SCANCODE_LEFT_CTRL = 0x1D, // not supported yet
    SCANCODE_LEFT_ALT = 0x38,  // not supported yet
} keyboard_scancode_t;

/**
 * Keyboard scancode to ASCII character mapping for standard US QWERTY layout.
 * Matches PS/2 Scan Code Set 1 (used by standard OS key handlers).
 */
static const char keyboard_map[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, /* 29: Left Control */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, /* 42: Left Shift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,                                                               /* 54: Right Shift */
    '*',                                                             /* 55: Keypad * */
    0,                                                               /* 56: Left Alt / Option */
    ' ',                                                             /* 57: Spacebar */
    0,                                                               /* 58: Caps Lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                    /* 59-68: F1 - F10 */
    0,                                                               /* 69: Num Lock */
    0,                                                               /* 70: Scroll Lock */
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.', /* 71-83: Numpad */
    0, 0, 0,
    0, 0, /* 87-88: F11 - F12 */
    [127] = 0};

/**
 * Keyboard scancode to ASCII character mapping for standard US QWERTY layout when shift is pressed.
 */
static const char keyboard_shift_map[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, /* 29: Left Control */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, /* 42: Left Shift */
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,                                                               /* 54: Right Shift */
    '*',                                                             /* 55: Keypad * */
    0,                                                               /* 56: Left Alt / Option */
    ' ',                                                             /* 57: Spacebar */
    0,                                                               /* 58: Caps Lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                    /* 59-68: F1 - F10 */
    0,                                                               /* 69: Num Lock */
    0,                                                               /* 70: Scroll Lock */
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.', /* 71-83: Numpad */
    0, 0, 0,
    0, 0, /* 87-88: F11 - F12 */
    [127] = 0};

/**
 * push a character to the keyboard buffer.
 * if the buffer is full, the character will be dropped.
 */
static void keyboard_buffer_push(char c)
{
    uint8_t next = (buffer_write + 1) % KEYBOARD_BUFFER_SIZE;

    // buffer full
    if (next == buffer_read)
    {
        return;
    }

    keyboard_buffer[buffer_write] = c;
    buffer_write = next;
}

/**
 * keyboard interrupt handler
 * reads the scancode from the keyboard controller and translates it to a character.
 * if the scancode corresponds to a key press, the character is pushed to the keyboard buffer
 * if the scancode corresponds to a key release, the key state is updated.
 */
static void keyboard_irq_handler(struct registers *regs)
{
    (void)regs;

    uint8_t scancode = io_inb(0x60);

    int released = scancode & 0x80;

    // remove release bit
    uint8_t key = scancode & 0x7F;

    /*
        handle modifier keys first.
        they do not produce characters.
    */
    switch (key)
    {
    case SCANCODE_LEFT_SHIFT:
    case SCANCODE_RIGHT_SHIFT:
        shift_state = released ? KEY_RELEASED : KEY_PRESSED;
        return;

    case SCANCODE_CAPS_LOCK:
        if (!released)
        {
            caps_lock_enabled = !caps_lock_enabled;
        }
        return;

    case SCANCODE_LEFT_CTRL:
        ctrl_state = released ? KEY_RELEASED : KEY_PRESSED;
        return;

    case SCANCODE_LEFT_ALT:
        alt_state = released ? KEY_RELEASED : KEY_PRESSED;
        return;
    }

    // ignore all key releases
    if (released)
        return;

    // if the key is a valid key
    if (key < 128)
    {
        char c = keyboard_map[key];

        // handle shift and caps lock
        if (c >= 'a' && c <= 'z')
        {
            int uppercase = shift_state ^ caps_lock_enabled;

            if (uppercase)
            {
                c = c - 'a' + 'A';
            }
        }
        // handle shift for non-alphabetic characters
        else if (shift_state == KEY_PRESSED)
        {
            c = keyboard_shift_map[key];
        }

        // push the character to the buffer if it is not 0 (null)
        if (c)
        {
            keyboard_buffer_push(c);
        }
    }
}

int keyboard_available(void)
{
    return buffer_read != buffer_write;
}

char keyboard_getchar(void)
{
    if (!keyboard_available())
    {
        return 0;
    }

    char c = keyboard_buffer[buffer_read];

    buffer_read =
        (buffer_read + 1) % KEYBOARD_BUFFER_SIZE;

    return c;
}

char keyboard_read(void)
{
    while (!keyboard_available())
    {
        asm volatile("hlt");
    }

    return keyboard_getchar();
}

void keyboard_initialize(void)
{
    irq_register_handler(1, keyboard_irq_handler);

    pic_unmask(1);
}