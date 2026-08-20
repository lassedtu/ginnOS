/**
 * @file keyboard.c
 * @brief PS/2 keyboard driver implementation.
 *
 * This file implements a PS/2 keyboard driver that handles keyboard interrupts, processes scan codes, and provides an interface for reading keyboard events. It supports modifier keys (Shift, Ctrl, Alt, AltGr) and special keys (arrows, home, end, etc.) and maintains a circular buffer of keyboard events.
 */

#include "keyboard.h"
#include "keyboard_layout.h"

#include "arch/x86/cpu/irq.h"
#include "arch/x86/cpu/isr.h"
#include "arch/x86/cpu/io.h"
#include "arch/x86/cpu/pic.h"
#include "common/stdint.h"

#define KEYBOARD_BUFFER_SIZE 128 // number of events the ring buffer can hold

static keyboard_event_t keyboard_buffer[KEYBOARD_BUFFER_SIZE]; // circular event buffer

static uint16_t buffer_read = 0;  // index of the next event to read
static uint16_t buffer_write = 0; // index of the next slot to write into

static uint32_t dropped_count = 0; // events dropped because the buffer was full

static key_state_t shift_state = KEY_RELEASED; // current shift key state
static key_state_t ctrl_state = KEY_RELEASED;  // current ctrl key state
static key_state_t alt_state = KEY_RELEASED;   // current left alt key state
static key_state_t altgr_state = KEY_RELEASED; // current right alt (AltGr) key state

static int caps_lock_enabled = 0; // caps lock toggle state

// set when a 0xE0 prefix byte has been read; cleared after the following byte is processed
static int extended_prefix_pending = 0;

// debug mode: when enabled, prints raw scancodes instead of translating
static int scancode_debug = 0;

// simple hex digit helper for debug output
static char hex_digit(uint8_t v)
{
    return v < 10 ? (char)('0' + v) : (char)('a' + v - 10);
}

typedef enum
{
    SCANCODE_LEFT_SHIFT = 0x2A,  // left shift key
    SCANCODE_RIGHT_SHIFT = 0x36, // right shift key
    SCANCODE_CAPS_LOCK = 0x3A,   // caps lock key
    SCANCODE_LEFT_CTRL = 0x1D,   // left control key
    SCANCODE_LEFT_ALT = 0x38,    // left alt key
} keyboard_scancode_t;

// extended scan codes (after 0xE0 prefix)
#define EXT_RIGHT_ALT 0x38  // right alt (AltGr)
#define EXT_RIGHT_CTRL 0x1D // right control

#define SCANCODE_EXTENDED_PREFIX 0xE0 // prefix byte for two-byte sequences

/**
 * extended scan code to keyboard_special_key_t mapping.
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
 */
static void keyboard_buffer_push(keyboard_event_t event)
{
    uint16_t next = (uint16_t)((buffer_write + 1) % KEYBOARD_BUFFER_SIZE);

    if (next == buffer_read)
    {
        dropped_count++;
        return;
    }

    keyboard_buffer[buffer_write] = event;
    buffer_write = next;
}

/**
 * keyboard IRQ handler.
 */
static void keyboard_irq_handler(struct registers *regs)
{
    (void)regs;

    uint8_t scancode = io_inb(0x60);
    const keyboard_layout_t *layout = keyboard_get_layout();

    // F12 (scancode 0x58 press) toggles debug mode
    if (scancode == 0x58)
    {
        scancode_debug = !scancode_debug;
        return;
    }
    if (scancode == 0xD8) // F12 release
        return;

    // in debug mode, print raw scancode as hex and push nothing
    if (scancode_debug && !(scancode & 0x80))
    {
        keyboard_event_t ev;
        // print format: [XX] or [E0 XX]
        ev.type = KEY_EVENT_CHAR;
        ev.character = '[';
        keyboard_buffer_push(ev);
        if (extended_prefix_pending)
        {
            ev.character = 'E';
            keyboard_buffer_push(ev);
            ev.character = '0';
            keyboard_buffer_push(ev);
            ev.character = ' ';
            keyboard_buffer_push(ev);
            extended_prefix_pending = 0;
        }
        ev.character = hex_digit((scancode & 0x7F) >> 4);
        keyboard_buffer_push(ev);
        ev.character = hex_digit((scancode & 0x7F) & 0x0F);
        keyboard_buffer_push(ev);
        ev.character = ']';
        keyboard_buffer_push(ev);
        return;
    }
    if (scancode_debug && (scancode & 0x80))
        return; // suppress releases in debug mode

    // 0xE0 prefix introduces a two-byte extended sequence
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

        // handle extended modifier keys
        if (key == EXT_RIGHT_ALT)
        {
            altgr_state = released ? KEY_RELEASED : KEY_PRESSED;
            return;
        }

        if (key == EXT_RIGHT_CTRL)
        {
            // treat as regular ctrl for now
            ctrl_state = released ? KEY_RELEASED : KEY_PRESSED;
            return;
        }

        // process extended key-press events (special keys)
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

    // handle modifier keys
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

    // determine which map to use
    // on macOS, both Left and Right Alt/Option produce special characters
    int altgr_active = (altgr_state == KEY_PRESSED || alt_state == KEY_PRESSED);
    char c = 0;

    if (altgr_active && shift_state == KEY_PRESSED)
    {
        c = layout->altgr_shift[key];
        if (c == 0)
            c = layout->altgr[key]; // fallback to altgr without shift
    }
    else if (altgr_active)
    {
        c = layout->altgr[key];
    }
    else if (shift_state == KEY_PRESSED)
    {
        c = layout->shift[key];
    }
    else
    {
        c = layout->normal[key];
    }

    if (c == 0)
        return;

    // ctrl+letter produces a control character (0x01–0x1A)
    // only when alt/altgr is not active
    if (ctrl_state == KEY_PRESSED && !altgr_active && c >= 'a' && c <= 'z')
    {
        c = (char)(c - 'a' + 1);
    }
    else if (ctrl_state == KEY_PRESSED && !altgr_active && c >= 'A' && c <= 'Z')
    {
        c = (char)(c - 'A' + 1);
    }
    else if (!altgr_active && !(shift_state == KEY_PRESSED))
    {
        // caps lock applies only to letters in the normal map
        if (c >= 'a' && c <= 'z' && caps_lock_enabled)
        {
            c = (char)(c - 'a' + 'A');
        }
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
