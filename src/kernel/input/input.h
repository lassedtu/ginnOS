#pragma once

/**
 * @file input.h
 * @brief the input core: /dev/input, one stream of key, button, motion
 * and wheel events from every input driver.
 *
 * drivers call input_report for each change and input_sync at the end of
 * a group. each open of /dev/input has its own queue, so several programs
 * can read the events; a reader that falls behind loses its oldest events
 * and gets SYN_DROPPED.
 */

#include "common/stdint.h"
#include "input_event.h"

/**
 * make /dev/input. call once, after device_registry_init.
 */
void input_init(void);

/**
 * report one change. safe in an interrupt handler.
 * @param type EV_KEY, EV_REL or EV_ABS.
 * @param code the key, button or axis.
 * @param value the new state or the change.
 */
void input_report(uint16_t type, uint16_t code, int32_t value);

/**
 * end a group of events (EV_SYN / SYN_REPORT). safe in an interrupt
 * handler.
 */
void input_sync(void);

/**
 * true while a reader holds the keyboard (INPUT_IOC_GRAB): the keyboard
 * driver then gives its keys only to /dev/input, not to the console.
 */
bool input_keyboard_grabbed(void);
