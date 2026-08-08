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
    KEY_PRESSED = 1
} key_state_t;

/**
 * initialize the keyboard driver and register the keyboard interrupt handler.
 * this function should be called after the IRQ subsystem is initialized.
 */
void keyboard_initialize(void);

/**
 * check if a character is available in the keyboard buffer.
 * @return 1 if a character is available, 0 otherwise.
 */
int keyboard_available(void);

/**
 * read a character from the keyboard buffer.
 * this function will return 0 if no character is available.
 * this function does not block.
 * @return the next character from the keyboard buffer, or 0 if no character is available.
 */
char keyboard_getchar(void);

/**
 * read a character from the keyboard buffer.
 * this function will block until a character is available.
 * @return the next character from the keyboard buffer.
 */
char keyboard_read(void);

/**
 * return the number of characters dropped due to a full buffer since boot.
 * the buffer still drops the newest keystroke when full; this counter lets
 * a caller detect and report overflow rather than silently losing input.
 * @return total number of dropped characters.
 */
uint32_t keyboard_dropped_count(void);