#pragma once

/**
 * @file doomgeneric_ginnos_window.h
 * @brief Doom in a window of the desktop: the parts the platform file uses.
 *
 * the keys are Linux key codes, so this header does not need the Linux
 * key names (they are the same as Doom's).
 */

#include <stdbool.h>
#include <stdint.h>

/**
 * a window for frames of width x height, if the desktop runs.
 * @return false without heimdalld: use the framebuffer.
 */
bool doom_window_open(int width, int height);

/** show a frame (0x00RRGGBB pixels, width x height as opened) */
void doom_window_frame(const uint32_t *pixels);

/**
 * the next key from the window.
 * @param code a Linux key code.
 * @return false when there is none.
 */
bool doom_window_key(int *pressed, unsigned *code);

void doom_window_title(const char *title);
