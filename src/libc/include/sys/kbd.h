#pragma once

#include <stdint.h>

/**
 * @file sys/kbd.h
 * @brief the ioctls of /dev/kbd: list, read and set the keyboard layout.
 *
 * the values and the struct are ABI and must match
 * src/drivers/keyboard/keyboard_ioctl.h.
 */

#define KBD_LAYOUT_NAME_MAX 32

/**
 * KBD_IOC_LAYOUT_NAME: set index; the kernel fills in name.
 */
struct kbd_layout_name
{
    uint32_t index;
    char name[KBD_LAYOUT_NAME_MAX];
};

#define KBD_IOC_LAYOUT_COUNT 0x4B80 // out: uint32_t, how many layouts there are
#define KBD_IOC_LAYOUT_NAME  0x4B81 // in/out: struct kbd_layout_name (EINVAL past the end)
#define KBD_IOC_GET_LAYOUT   0x4B82 // out: uint32_t, the index of the active layout
#define KBD_IOC_SET_LAYOUT   0x4B83 // in: uint32_t, the index to make active
