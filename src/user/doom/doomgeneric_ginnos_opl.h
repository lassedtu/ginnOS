#pragma once

/**
 * @file doomgeneric_ginnos_opl.h
 * @brief the music samples from the OPL emulator (doomgeneric_ginnos_opl.c).
 */

#include <stdint.h>

/**
 * make the next frames of music (16-bit stereo, at the rate given to
 * OPL_SetSampleRate) and run the music callbacks that fall in them.
 * gives silence before the music player has started the emulator.
 */
void opl_ginnos_generate(int16_t *out, unsigned int frames);
