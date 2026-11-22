#pragma once

/**
 * @file keyboard_ioctl.h
 * @brief the ioctls of /dev/kbd: list, read and set the keyboard layout.
 *
 * the values and the struct are ABI; libc's <sys/kbd.h> has the same.
 */

#include "common/stdint.h"

#define KBD_LAYOUT_NAME_MAX 32

/**
 * KBD_IOC_LAYOUT_NAME: the caller sets index; the kernel fills in name.
 */
struct kbd_layout_name
{
    uint32_t index;
    char name[KBD_LAYOUT_NAME_MAX];
};

#define KBD_IOC_LAYOUT_COUNT 0x4B80 // out: uint32_t, how many layouts there are
#define KBD_IOC_LAYOUT_NAME  0x4B81 // in/out: struct kbd_layout_name (-EINVAL past the end)
#define KBD_IOC_GET_LAYOUT   0x4B82 // out: uint32_t, the index of the active layout
#define KBD_IOC_SET_LAYOUT   0x4B83 // in: uint32_t, the index to make active
#define KBD_IOC_GET_KEYMAP   0x4B84 // out: struct kbd_keymap, the active layout's tables

#define KBD_KEYMAP_KEYS 128

/**
 * KBD_IOC_GET_KEYMAP: the characters of the active layout, by key code
 * (the Linux key code of /dev/input, below 128). 0: the key gives no
 * character. the characters are ASCII. altgr is for either Alt key (the
 * Option keys on a Mac keyboard), as on the text console.
 */
struct kbd_keymap
{
    char normal[KBD_KEYMAP_KEYS];
    char shift[KBD_KEYMAP_KEYS];
    char altgr[KBD_KEYMAP_KEYS];
    char altgr_shift[KBD_KEYMAP_KEYS];
};
