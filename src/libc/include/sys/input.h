#pragma once

/**
 * @file sys/input.h
 * @brief the events of /dev/input (ABI).
 *
 * one event is 12 bytes: a time, a type, a code and a value. the types and
 * codes are the ones of Linux (linux/input-event-codes.h), so ported
 * programs know them. a group of events that belong together (a mouse
 * moving in x and y, and a button) ends with EV_SYN / SYN_REPORT.
 *
 * the kernel has a copy in src/kernel/input/input_event.h; the two must
 * stay the same.
 */

#include <stdint.h>

typedef struct
{
    uint32_t time_ms; // milliseconds since boot
    uint16_t type;    // EV_
    uint16_t code;    // KEY_, BTN_, REL_ or ABS_
    int32_t value;    // keys: 1 down, 0 up, 2 repeat; REL: a change; ABS: a position
} input_event_t;

// event types
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03

// EV_SYN codes
#define SYN_REPORT  0 // the end of a group of events
#define SYN_DROPPED 3 // the reader was too slow: events were lost before this

// EV_REL codes
#define REL_X     0x00
#define REL_Y     0x01 // positive is down
#define REL_WHEEL 0x08 // positive is up (away from the user)

// EV_ABS codes: positions from 0 to INPUT_ABS_MAX across the screen
#define ABS_X 0x00
#define ABS_Y 0x01
#define INPUT_ABS_MAX 32767

// mouse buttons (EV_KEY)
#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112

// some key codes (EV_KEY); the others are Linux's numbers too
#define KEY_ESC       1
#define KEY_BACKSPACE 14
#define KEY_TAB       15
#define KEY_ENTER     28
#define KEY_LEFTCTRL  29
#define KEY_LEFTSHIFT 42
#define KEY_RIGHTSHIFT 54
#define KEY_LEFTALT   56
#define KEY_SPACE     57
#define KEY_F1        59
#define KEY_F12       88
#define KEY_RIGHTCTRL 97
#define KEY_RIGHTALT  100
#define KEY_HOME      102
#define KEY_UP        103
#define KEY_PAGEUP    104
#define KEY_LEFT      105
#define KEY_RIGHT     106
#define KEY_END       107
#define KEY_DOWN      108
#define KEY_PAGEDOWN  109
#define KEY_INSERT    110
#define KEY_DELETE    111
#define KEY_LEFTMETA  125

// ioctl: while a reader holds the grab, key events go only to /dev/input,
// not to the text console (the compositor takes the keyboard). int arg: 1
// grab, 0 release. closing the reader releases it.
#define INPUT_IOC_GRAB 0x40044901u
