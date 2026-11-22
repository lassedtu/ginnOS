/**
 * @file keyboard.c
 * @brief PS/2 keyboard driver implementation.
 *
 * This file implements a PS/2 keyboard driver that handles keyboard interrupts, processes scan codes, and provides an interface for reading keyboard events. It supports modifier keys (Shift, Ctrl, Alt, AltGr) and special keys (arrows, home, end, etc.) and maintains a circular buffer of keyboard events.
 */

#include "keyboard.h"
#include "keyboard_layout.h"
#include "keyboard_ioctl.h"
#include "kernel/errno.h"
#include "kernel/usermode/uaccess.h"
#include "kernel/vfs/poll.h"
#include "kernel/input/input.h"
#include "drivers/mouse/ps2_mouse.h"
#include "kernel/signal/signal.h"

#include "arch/arch.h"
#include "kernel/irq/irq.h"
#include "kernel/device/device.h"
#include "kernel/process/process.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/sync/spinlock.h"
#include "kernel/sync/wait_queue.h"
#include "arch/x86/cpu/io.h"
#include "common/stdint.h"
#include "common/string.h"
#include "common/memory.h"

#define KEYBOARD_BUFFER_SIZE 128 // number of events the ring buffer can hold

static keyboard_event_t keyboard_buffer[KEYBOARD_BUFFER_SIZE]; // circular event buffer

static uint16_t buffer_read = 0;  // index of the next event to read
static uint16_t buffer_write = 0; // index of the next slot to write into

static uint32_t dropped_count = 0; // events dropped because the buffer was full

// guards buffer_read, buffer_write and dropped_count. the IRQ handler
// produces and any number of processes consume (sys_read, sys_pollkey), so
// taking an event must be one atomic check-and-advance.
static spinlock_t buffer_lock;

// processes sleeping in keyboard_wait_event until the IRQ handler adds an
// event.
static wait_queue_t readers;

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
static keyboard_filter_fn event_filter;

// report KEY_EVENT_RAW events instead of characters (keyboard_set_raw_events)
static volatile bool raw_events;

void keyboard_set_raw_events(bool on)
{
    raw_events = on;
}

static void keyboard_buffer_push(keyboard_event_t event)
{
    if (input_keyboard_grabbed())
    {
        return; // a program (the compositor) has the keyboard through /dev/input
    }
    if (raw_events != (event.type == KEY_EVENT_RAW))
    {
        return; // only the kind of event that is on goes into the buffer
    }

    if (event_filter && event_filter(&event))
    {
        return; // the terminal took it (a signal key)
    }

    uint32_t flags = spin_lock_irqsave(&buffer_lock);

    uint16_t next = (uint16_t)((buffer_write + 1) % KEYBOARD_BUFFER_SIZE);
    if (next == buffer_read)
    {
        dropped_count++;
    }
    else
    {
        keyboard_buffer[buffer_write] = event;
        buffer_write = next;
        // every sleeping reader re-checks; whoever runs first takes it
        wait_queue_wake_all(&readers);
        poll_notify();
    }

    spin_unlock_irqrestore(&buffer_lock, flags);
}

/**
 * take the oldest event out of the buffer. caller holds buffer_lock.
 * @return true if an event was taken, false if the buffer was empty.
 */
static bool buffer_pop_locked(keyboard_event_t *event_out)
{
    if (buffer_read == buffer_write)
    {
        return false;
    }

    *event_out = keyboard_buffer[buffer_read];
    buffer_read = (uint16_t)((buffer_read + 1) % KEYBOARD_BUFFER_SIZE);
    return true;
}

/**
 * keyboard IRQ handler: one byte from the PS/2 controller.
 */
static void keyboard_irq_handler(uint32_t irq, trap_frame_t *frame)
{
    (void)irq;
    (void)frame;

    // the data port is shared with the mouse (the aux port): the status
    // says which device the byte is from
    uint8_t status = io_inb(0x64);
    if ((status & 0x21) == 0x21)
    {
        ps2_mouse_byte(io_inb(0x60));
        return;
    }
    keyboard_handle_scancode(io_inb(0x60));
}

// the keys that are down, by Linux key code (a press of a key that is
// already down is a repeat)
static uint32_t keys_down[256 / 32];

/**
 * the Linux key code of a set-1 scan code: the same number for the
 * normal keys, a table for the ones after an E0 prefix.
 * @return the code, or 0 for a key without one.
 */
static uint16_t linux_key_code(uint8_t key, bool extended)
{
    if (!extended)
    {
        return key <= 0x58 ? key : 0;
    }
    switch (key)
    {
    case 0x1C: return 96;  // keypad Enter
    case 0x1D: return 97;  // right Ctrl
    case 0x35: return 98;  // keypad /
    case 0x37: return 99;  // Print Screen
    case 0x38: return 100; // right Alt (AltGr)
    case 0x47: return 102; // Home
    case 0x48: return 103; // Up
    case 0x49: return 104; // Page Up
    case 0x4B: return 105; // Left
    case 0x4D: return 106; // Right
    case 0x4F: return 107; // End
    case 0x50: return 108; // Down
    case 0x51: return 109; // Page Down
    case 0x52: return 110; // Insert
    case 0x53: return 111; // Delete
    case 0x5B: return 125; // left Meta (Windows, Command)
    case 0x5C: return 126; // right Meta
    case 0x5D: return 127; // Menu
    default:   return 0;
    }
}

/**
 * give a key change to /dev/input (value 2 for a key that is held).
 */
static void report_key(uint8_t key, bool extended, bool pressed)
{
    uint16_t code = linux_key_code(key, extended);
    if (code == 0)
    {
        return;
    }
    uint32_t bit = 1u << (code % 32);
    int32_t value = 0;
    if (pressed)
    {
        value = (keys_down[code / 32] & bit) ? 2 : 1;
        keys_down[code / 32] |= bit;
    }
    else
    {
        keys_down[code / 32] &= ~bit;
    }
    input_report(EV_KEY, code, value);
    input_sync();
}

void keyboard_handle_scancode(uint8_t scancode)
{
    const keyboard_layout_t *layout = keyboard_get_layout();

    // F12 (scancode 0x58 press) toggles debug mode
    // (not while a program has the keyboard: there F12 is a key like the others)
    bool own_keys = !raw_events && !input_keyboard_grabbed();
    if (scancode == 0x58 && own_keys)
    {
        scancode_debug = !scancode_debug;
        return;
    }
    if (scancode == 0xD8 && own_keys) // F12 release
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

    report_key(key, extended_prefix_pending != 0, !released);

    if (raw_events)
    {
        keyboard_event_t event;
        event.type = KEY_EVENT_RAW;
        event.raw.code = (uint8_t)(key | (extended_prefix_pending ? 0x80 : 0));
        event.raw.pressed = released ? 0 : 1;
        keyboard_buffer_push(event);
        // the code below keeps the modifier state up to date; its
        // character events are not pushed while raw events are on
    }

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
    uint32_t flags = spin_lock_irqsave(&buffer_lock);
    bool got = buffer_pop_locked(event_out);
    spin_unlock_irqrestore(&buffer_lock, flags);

    return got ? 1 : 0;
}

bool keyboard_wait_event(keyboard_event_t *event_out)
{
    uint32_t flags = spin_lock_irqsave(&buffer_lock);

    while (!buffer_pop_locked(event_out))
    {
        if (process_current() && scheduler_is_active())
        {
            if (signal_pending(process_current()))
            {
                spin_unlock_irqrestore(&buffer_lock, flags);
                return false;
            }
            // sleep until the IRQ handler pushes an event and wakes us
            wait_queue_block_locked_interruptible(&readers, &buffer_lock);
        }
        else
        {
            // nothing to switch to yet (the first shell runs on the boot
            // stack until it starts a child): wait for the next interrupt
            // with the lock released and interrupts enabled
            spin_unlock_irqrestore(&buffer_lock, flags);
            arch_enable_interrupts();
            arch_halt();
            flags = spin_lock_irqsave(&buffer_lock);
        }
    }

    spin_unlock_irqrestore(&buffer_lock, flags);
    return true;
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

int keyboard_read(void)
{
    keyboard_event_t event;

    for (;;)
    {
        if (!keyboard_wait_event(&event))
            return -1;
        if (event.type == KEY_EVENT_CHAR)
            return (unsigned char)event.character;
    }
}

void keyboard_set_filter(keyboard_filter_fn filter)
{
    event_filter = filter;
}

void keyboard_flush(void)
{
    uint32_t flags = spin_lock_irqsave(&buffer_lock);
    buffer_read = buffer_write;
    spin_unlock_irqrestore(&buffer_lock, flags);
}

uint32_t keyboard_dropped_count(void)
{
    return dropped_count;
}

/**
 * the ioctls of /dev/kbd (keyboard_ioctl.h).
 */
static int32_t keyboard_ioctl(device_t *dev, uint32_t request, void *arg)
{
    (void)dev;
    switch (request)
    {
    case KBD_IOC_LAYOUT_COUNT:
    {
        uint32_t count = 0;
        while (keyboard_layout_at(count))
        {
            count++;
        }
        return kerr_to_errno(copy_to_user(arg, &count, sizeof(count)));
    }

    case KBD_IOC_LAYOUT_NAME:
    {
        struct kbd_layout_name info;
        if (kerr_failed(copy_from_user(&info, arg, sizeof(info))))
        {
            return -EFAULT;
        }
        const keyboard_layout_t *layout = keyboard_layout_at(info.index);
        if (!layout)
        {
            return -EINVAL;
        }
        memset(info.name, 0, sizeof(info.name));
        strncpy(info.name, layout->name, sizeof(info.name) - 1);
        return kerr_to_errno(copy_to_user(arg, &info, sizeof(info)));
    }

    case KBD_IOC_GET_LAYOUT:
    {
        uint32_t index = keyboard_layout_index();
        return kerr_to_errno(copy_to_user(arg, &index, sizeof(index)));
    }

    case KBD_IOC_SET_LAYOUT:
    {
        uint32_t index;
        if (kerr_failed(copy_from_user(&index, arg, sizeof(index))))
        {
            return -EFAULT;
        }
        return keyboard_set_layout(index) ? 0 : -EINVAL;
    }

    case KBD_IOC_GET_KEYMAP:
    {
        // the layout tables are indexed by scan code set 1, which is the
        // Linux key code for the keys that have characters
        const keyboard_layout_t *layout = keyboard_get_layout();
        struct kbd_keymap map;
        memcpy(map.normal, layout->normal, sizeof(map.normal));
        memcpy(map.shift, layout->shift, sizeof(map.shift));
        memcpy(map.altgr, layout->altgr, sizeof(map.altgr));
        memcpy(map.altgr_shift, layout->altgr_shift, sizeof(map.altgr_shift));
        return kerr_to_errno(copy_to_user(arg, &map, sizeof(map)));
    }

    default:
        return -ENOTTY;
    }
}

static const device_ops_t keyboard_ops = {
    .ioctl = keyboard_ioctl,
};

void keyboard_initialize(void)
{
    spinlock_init(&buffer_lock);
    wait_queue_init(&readers);

    // register the keyboard handler on IRQ1; irq_request unmasks the line.
    irq_request(1, keyboard_irq_handler, IRQ_FLAG_NONE, "keyboard");

    // announce ourselves to the device registry.
    static device_t keyboard_device;
    strncpy(keyboard_device.name, "kbd", DEVICE_NAME_MAX - 1);
    keyboard_device.name[DEVICE_NAME_MAX - 1] = '\0';
    keyboard_device.type = DEVICE_TYPE_INPUT;
    keyboard_device.ops = &keyboard_ops;
    keyboard_device.driver_data = 0;
    device_register(&keyboard_device);
}
