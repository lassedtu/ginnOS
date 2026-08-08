#pragma once
#include "../../common/stdint.h"

/**
 * keyboard key state enumeration
 * KEY_RELEASED: the key is not pressed
 * KEY_PRESSED: the key is pressed
 */
typedef enum
{
    KEY_RELEASED = 0,
    KEY_PRESSED  = 1,
} key_state_t;

/**
 * special key codes for keys that cannot be represented as a plain char.
 * produced by 0xE0-prefixed PS/2 scan code sequences.
 */
typedef enum
{
    KEY_ARROW_UP    = 1,
    KEY_ARROW_DOWN  = 2,
    KEY_ARROW_LEFT  = 3,
    KEY_ARROW_RIGHT = 4,
    KEY_HOME        = 5,
    KEY_END         = 6,
    KEY_PAGE_UP     = 7,
    KEY_PAGE_DOWN   = 8,
    KEY_INSERT      = 9,
    KEY_DELETE      = 10,
} keyboard_special_key_t;

/**
 * event type tag for keyboard_event_t.
 * KEY_EVENT_CHAR: the event carries a printable/control character in .character.
 * KEY_EVENT_SPECIAL: the event carries a special key code in .special.
 */
typedef enum
{
    KEY_EVENT_CHAR    = 0,
    KEY_EVENT_SPECIAL = 1,
} keyboard_event_type_t;

/**
 * a single keyboard event returned by keyboard_read_event().
 * check .type before reading either union member.
 */
typedef struct
{
    keyboard_event_type_t type;
    union
    {
        char                   character; // valid when type == KEY_EVENT_CHAR
        keyboard_special_key_t special;   // valid when type == KEY_EVENT_SPECIAL
    };
} keyboard_event_t;

/**
 * initialize the keyboard driver and register the keyboard interrupt handler.
 * should be called after the IRQ subsystem is initialized.
 */
void keyboard_initialize(void);

/**
 * check if at least one event is available in the event buffer.
 * @return 1 if an event is available, 0 otherwise.
 */
int keyboard_available(void);

/**
 * read the next character from the event buffer without blocking.
 * special key events (arrow keys, etc.) are discarded; only KEY_EVENT_CHAR
 * events are returned here. callers that need special keys should use
 * keyboard_read_event() instead.
 * @return the next character, or 0 if no character event is available.
 */
char keyboard_getchar(void);

/**
 * read the next character from the event buffer, blocking until one arrives.
 * special key events are discarded the same way as keyboard_getchar().
 * @return the next character.
 */
char keyboard_read(void);

/**
 * read the next keyboard event from the event buffer without blocking.
 * @param event_out output structure filled with the event type and value.
 * @return 1 if an event was read, 0 if the buffer was empty.
 */
int keyboard_read_event(keyboard_event_t *event_out);

/**
 * read the next keyboard event from the event buffer, blocking until one arrives.
 * @param event_out output structure filled with the event type and value.
 */
void keyboard_wait_event(keyboard_event_t *event_out);

/**
 * return the number of events dropped due to a full buffer since boot.
 * @return total number of dropped events.
 */
uint32_t keyboard_dropped_count(void);
