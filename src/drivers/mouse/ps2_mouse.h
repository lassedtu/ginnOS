#pragma once

/**
 * @file ps2_mouse.h
 * @brief the PS/2 mouse on the i8042 controller's second (aux) port.
 *
 * the mouse sends 3-byte packets (4 with a scroll wheel) on IRQ 12. the
 * driver turns them into /dev/input events: BTN_LEFT, BTN_RIGHT and
 * BTN_MIDDLE, REL_X, REL_Y and REL_WHEEL.
 */

#include "common/stdint.h"

/**
 * find and turn on a PS/2 mouse. call with interrupts off, after the
 * keyboard driver and the interrupt controllers are set up.
 * @return true if a mouse answered.
 */
bool ps2_mouse_init(void);

/**
 * one byte from the mouse. the keyboard interrupt hands over a mouse byte
 * it finds in the data port (the i8042 status says which device sent it).
 */
void ps2_mouse_byte(uint8_t byte);
