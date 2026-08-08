#include "keyboard.h"

#include "../../arch/x86/cpu/irq.h"
#include "../../arch/x86/cpu/isr.h"
#include "../../arch/x86/cpu/io.h"
#include "../../arch/x86/cpu/pic.h"
#include "../../common/stdint.h"

#define KEYBOARD_BUFFER_SIZE 128 // number of events the ring buffer can hold

static keyboard_event_t keyboard_buffer[KEYBOARD_BUFFER_SIZE]; // circular event buffer

static uint16_t buffer_read = 0;  // index of the next event to read
static uint16_t buffer_write = 0; // index of the next slot to write into

static uint32_t dropped_count = 0; // events dropped because the buffer was full

static key_state_t shift_state = KEY_RELEASED; // current shift key state
static key_state_t ctrl_state = KEY_RELEASED;  // current ctrl key state
static key_state_t alt_state = KEY_RELEASED;   // current alt key state

static int caps_lock_enabled = 0; // caps lock toggle state

// set when a 0xE0 prefix byte has been read; cleared after the following byte is processed
static int extended_prefix_pending = 0;

/**
 * PS/2 scan code set 1 — standard US QWERTY layout.
 * index is the scan code (0x00–0x7F); value is the ASCII character, or 0 for
 * keys that don't produce a printable character.
 */
typedef enum
{
    SCANCODE_LEFT_SHIFT = 0x2A,  // left shift key
    SCANCODE_RIGHT_SHIFT = 0x36, // right shift key
    SCANCODE_CAPS_LOCK = 0x3A,   // caps lock key
    SCANCODE_LEFT_CTRL = 0x1D,   // left control key
    SCANCODE_LEFT_ALT = 0x38,    // left alt key
} keyboard_scancode_t;

#define SCANCODE_EXTENDED_PREFIX 0xE0 // prefix byte that introduces a two-byte extended sequence

/**
 * PS/2 scan code set 1: standard US QWERTY layout.
 * index is the scan code (0x00–0x7F); value is the ASCII character, or 0 for
 * keys that don't produce a printable character.
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
    0,                                                               /* 56: Left Alt */
    ' ',                                                             /* 57: Spacebar */
    0,                                                               /* 58: Caps Lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                    /* 59-68: F1–F10 */
    0,                                                               /* 69: Num Lock */
    0,                                                               /* 70: Scroll Lock */
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.', /* 71-83: Numpad */
    0, 0, 0,
    0, 0, /* 87-88: F11–F12 */
    [127] = 0};

/**
 * PS/2 scan code set 1 — standard US QWERTY layout with shift held.
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
    0,                                                               /* 56: Left Alt */
    ' ',                                                             /* 57: Spacebar */
    0,                                                               /* 58: Caps Lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                    /* 59-68: F1–F10 */
    0,                                                               /* 69: Num Lock */
    0,                                                               /* 70: Scroll Lock */
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.', /* 71-83: Numpad */
    0, 0, 0,
    0, 0, /* 87-88: F11–F12 */
    [127] = 0};

/**
 * extended scan code (0xE0 xx) to keyboard_special_key_t mapping.
 * index is the second byte of the sequence (0x00–0x7F); value is the
 * special key code, or 0 if the sequence is unrecognised.
 */
static const keyboard_special_key_t extended_map[128] = {
    [0x48] = KEY_ARROW_UP,
    [0x50] = KEY_ARROW_DOWN,
    [0x4B] = KEY_ARROW_LEFT,
    [0x4D] = KEY_ARROW_RIGHT,
    [0x47] = KEY_HOME,
    [0x4F] = KEY_END,
    [0x49] = KEY_PAGE_UP,
    [0x51] = KEY_PAGE_DOWN,
    [0x52] = KEY_INSERT,
    [0x53] = KEY_DELETE,
};

/**
 * push a keyboard event into the circular buffer.
 * @param event the keyboard event to push.
 */
static void keyboard_buffer_push(keyboard_event_t event)
{
    uint16_t next = (uint16_t)((buffer_write + 1) % KEYBOARD_BUFFER_SIZE);

    if (next == buffer_read)
    {
        // buffer full — drop and record
        dropped_count++;
        return;
    }

    keyboard_buffer[buffer_write] = event;
    buffer_write = next;
}

/**
 * keyboard IRQ handler called on IRQ1 (keyboard).
 * reads the scan code from the keyboard controller and translates it into
 * a keyboard_event_t, which is pushed into the circular buffer.
 * handles modifier keys (shift, ctrl, alt) and caps lock state.
 * handles extended scan codes (0xE0 prefix) for arrow keys, home/end,
 * page up/down, insert/delete, etc.
 * @param regs pointer to the CPU registers at the time of the interrupt (unused).
 */
static void keyboard_irq_handler(struct registers *regs)
{
    (void)regs;

    uint8_t scancode = io_inb(0x60);

    // a 0xE0 byte introduces a two-byte extended sequence — record and wait
    // for the next IRQ to deliver the actual scan code
    if (scancode == SCANCODE_EXTENDED_PREFIX)
    {
        extended_prefix_pending = 1;
        return;
    }

    int released = scancode & 0x80;
    uint8_t key = scancode & 0x7F;

    if (extended_prefix_pending)
    {
        extended_prefix_pending = 0;

        // only process key-press events for extended keys (ignore releases)
        if (!released && key < 128)
        {
            keyboard_special_key_t special = extended_map[key];
            if (special != 0)
            {
                keyboard_event_t event;
                event.type = KEY_EVENT_SPECIAL;
                event.special = special;
                keyboard_buffer_push(event);
            }
        }
        return;
    }

    // handle modifier keys — they update state but do not produce events
    switch (key)
    {
    case SCANCODE_LEFT_SHIFT:
    case SCANCODE_RIGHT_SHIFT:
        shift_state = released ? KEY_RELEASED : KEY_PRESSED;
        return;

    case SCANCODE_CAPS_LOCK:
        if (!released)
            caps_lock_enabled = !caps_lock_enabled;
        return;

    case SCANCODE_LEFT_CTRL:
        ctrl_state = released ? KEY_RELEASED : KEY_PRESSED;
        return;

    case SCANCODE_LEFT_ALT:
        alt_state = released ? KEY_RELEASED : KEY_PRESSED;
        return;
    }

    // ignore key release events for all other keys
    if (released)
        return;

    if (key >= 128)
        return;

    char c = keyboard_map[key];

    if (c == 0)
        return;

    // ctrl+letter produces a control character (0x01–0x1A)
    if (ctrl_state == KEY_PRESSED && c >= 'a' && c <= 'z')
    {
        c = (char)(c - 'a' + 1);
    }
    else if (ctrl_state == KEY_PRESSED && c >= 'A' && c <= 'Z')
    {
        c = (char)(c - 'A' + 1);
    }
    else if (c >= 'a' && c <= 'z')
    {
        // caps lock and shift each individually toggle uppercase;
        // holding both cancels out (XOR behaviour)
        if ((int)shift_state ^ caps_lock_enabled)
            c = (char)(c - 'a' + 'A');
    }
    else if (shift_state == KEY_PRESSED)
    {
        c = keyboard_shift_map[key];
    }

    keyboard_event_t event;
    event.type = KEY_EVENT_CHAR;
    event.character = c;
    keyboard_buffer_push(event);
}

int keyboard_available(void)
{
    return buffer_read != buffer_write;
}

int keyboard_read_event(keyboard_event_t *event_out)
{
    if (!keyboard_available())
        return 0;

    *event_out = keyboard_buffer[buffer_read];
    buffer_read = (uint16_t)((buffer_read + 1) % KEYBOARD_BUFFER_SIZE);
    return 1;
}

void keyboard_wait_event(keyboard_event_t *event_out)
{
    while (!keyboard_available())
        __asm__ __volatile__("hlt");

    keyboard_read_event(event_out);
}

char keyboard_getchar(void)
{
    keyboard_event_t event;

    // skip events until we find a character event or the buffer empties
    while (keyboard_available())
    {
        if (keyboard_read_event(&event) && event.type == KEY_EVENT_CHAR)
            return event.character;
    }

    return 0;
}

char keyboard_read(void)
{
    keyboard_event_t event;

    for (;;)
    {
        keyboard_wait_event(&event);
        if (event.type == KEY_EVENT_CHAR)
            return event.character;
        // special key events are skipped; caller should use keyboard_wait_event
        // directly if they need arrow keys / home / end / etc.
    }
}

uint32_t keyboard_dropped_count(void)
{
    return dropped_count;
}

void keyboard_initialize(void)
{
    irq_register_handler(1, keyboard_irq_handler);
    pic_unmask(1);
}
