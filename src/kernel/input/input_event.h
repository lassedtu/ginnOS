#pragma once

/**
 * @file input_event.h
 * @brief the events of /dev/input (ABI).
 *
 * one event is 12 bytes: a time, a type, a code and a value. the types and
 * codes are the ones of Linux (linux/input-event-codes.h), so ported
 * programs know them. a group of events that belong together (a mouse
 * moving in x and y, and a button) ends with EV_SYN / SYN_REPORT.
 *
 * libc has a copy in <sys/input.h>; the two must stay the same.
 */

#include "common/stdint.h"

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

// ioctl: while a reader holds the grab, key events go only to /dev/input,
// not to the text console (the compositor takes the keyboard). int arg: 1
// grab, 0 release. closing the reader releases it.
#define INPUT_IOC_GRAB 0x40044901u
