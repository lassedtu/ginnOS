/**
 * @file keyboard_layout.h
 * @brief Keyboard layout definitions.
 *
 * This file defines the structure for keyboard layouts, which map PS/2 scan codes to characters. It includes support for normal, shift, and AltGr (Right Alt / Option) mappings, as well as a combined AltGr + Shift mapping.
 */

#pragma once

/**
 * a keyboard layout maps PS/2 scan codes to characters.
 * three maps: normal, shift, and altgr (Option on Mac).
 * a fourth map (altgr+shift) is also provided for { and }.
 */
typedef struct
{
    const char *name;
    const char normal[128];
    const char shift[128];
    const char altgr[128];       // AltGr / Right Alt / Option
    const char altgr_shift[128]; // AltGr + Shift
} keyboard_layout_t;

/**
 * get the active keyboard layout.
 * @return pointer to the active layout.
 */
const keyboard_layout_t *keyboard_get_layout(void);
