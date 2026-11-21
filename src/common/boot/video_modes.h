#pragma once

/**
 * @file video_modes.h
 * @brief the video modes the firmware offers, as the bootloader reports them.
 *
 * the limine boot code fills the list (from the framebuffer response,
 * revision 1 and later); other boot paths leave it empty. the fb driver
 * shows it to user space (FBIOGET_MODE).
 */

#include "common/stdint.h"

#define BOOT_VIDEO_MODES_MAX 64

typedef struct
{
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
} boot_video_mode_t;

/**
 * the list of modes.
 * @param modes receives a pointer to the first mode.
 * @return the number of modes (0 if the bootloader gave none).
 */
uint32_t boot_video_modes(const boot_video_mode_t **modes);
