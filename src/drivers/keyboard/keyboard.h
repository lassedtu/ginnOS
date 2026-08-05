#pragma once

/**
 * initialize the keyboard driver and register the keyboard interrupt handler.
 * this function should be called after the IRQ subsystem is initialized.
 */
void keyboard_initialize(void);