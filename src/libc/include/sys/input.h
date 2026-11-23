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

// the key codes of a PC keyboard (EV_KEY), as Linux numbers them
#define KEY_ESC         1
#define KEY_1           2
#define KEY_2           3
#define KEY_3           4
#define KEY_4           5
#define KEY_5           6
#define KEY_6           7
#define KEY_7           8
#define KEY_8           9
#define KEY_9           10
#define KEY_0           11
#define KEY_MINUS       12
#define KEY_EQUAL       13
#define KEY_BACKSPACE   14
#define KEY_TAB         15
#define KEY_Q           16
#define KEY_W           17
#define KEY_E           18
#define KEY_R           19
#define KEY_T           20
#define KEY_Y           21
#define KEY_U           22
#define KEY_I           23
#define KEY_O           24
#define KEY_P           25
#define KEY_LEFTBRACE   26
#define KEY_RIGHTBRACE  27
#define KEY_ENTER       28
#define KEY_LEFTCTRL    29
#define KEY_A           30
#define KEY_S           31
#define KEY_D           32
#define KEY_F           33
#define KEY_G           34
#define KEY_H           35
#define KEY_J           36
#define KEY_K           37
#define KEY_L           38
#define KEY_SEMICOLON   39
#define KEY_APOSTROPHE  40
#define KEY_GRAVE       41
#define KEY_LEFTSHIFT   42
#define KEY_BACKSLASH   43
#define KEY_Z           44
#define KEY_X           45
#define KEY_C           46
#define KEY_V           47
#define KEY_B           48
#define KEY_N           49
#define KEY_M           50
#define KEY_COMMA       51
#define KEY_DOT         52
#define KEY_SLASH       53
#define KEY_RIGHTSHIFT  54
#define KEY_KPASTERISK  55
#define KEY_LEFTALT     56
#define KEY_SPACE       57
#define KEY_CAPSLOCK    58
#define KEY_F1          59
#define KEY_F2          60
#define KEY_F3          61
#define KEY_F4          62
#define KEY_F5          63
#define KEY_F6          64
#define KEY_F7          65
#define KEY_F8          66
#define KEY_F9          67
#define KEY_F10         68
#define KEY_NUMLOCK     69
#define KEY_SCROLLLOCK  70
#define KEY_KP7         71
#define KEY_KP8         72
#define KEY_KP9         73
#define KEY_KPMINUS     74
#define KEY_KP4         75
#define KEY_KP5         76
#define KEY_KP6         77
#define KEY_KPPLUS      78
#define KEY_KP1         79
#define KEY_KP2         80
#define KEY_KP3         81
#define KEY_KP0         82
#define KEY_KPDOT       83
#define KEY_102ND       86
#define KEY_F11         87
#define KEY_F12         88
#define KEY_KPENTER     96
#define KEY_RIGHTCTRL   97
#define KEY_KPSLASH     98
#define KEY_SYSRQ       99
#define KEY_RIGHTALT    100
#define KEY_HOME        102
#define KEY_UP          103
#define KEY_PAGEUP      104
#define KEY_LEFT        105
#define KEY_RIGHT       106
#define KEY_END         107
#define KEY_DOWN        108
#define KEY_PAGEDOWN    109
#define KEY_INSERT      110
#define KEY_DELETE      111
#define KEY_LEFTMETA    125
#define KEY_RIGHTMETA   126
#define KEY_COMPOSE     127

// ioctl: while a reader holds the grab, key events go only to /dev/input,
// not to the text console (the compositor takes the keyboard). int arg: 1
// grab, 0 release. closing the reader releases it.
#define INPUT_IOC_GRAB 0x40044901u
