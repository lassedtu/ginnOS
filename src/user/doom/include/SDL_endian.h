#pragma once

/**
 * @file SDL_endian.h
 * @brief a stand-in for SDL's byte order header.
 *
 * Chocolate Doom's i_swap.h and midifile.c (the OPL music, see
 * src/user/doom/Makefile) take their byte swapping from SDL. ginnOS runs
 * on x86 only, which is little endian, so the little-endian swaps do
 * nothing and the big-endian ones reverse the bytes.
 */

#include <stdint.h>

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER  SDL_LIL_ENDIAN

#define SDL_SwapLE16(x) ((uint16_t)(x))
#define SDL_SwapLE32(x) ((uint32_t)(x))
#define SDL_SwapBE16(x) ((uint16_t)((((uint16_t)(x)) >> 8) | (((uint16_t)(x)) << 8)))
#define SDL_SwapBE32(x) ((uint32_t)(((uint32_t)(x) >> 24) | (((uint32_t)(x) >> 8) & 0xFF00u) | \
                                    (((uint32_t)(x) << 8) & 0xFF0000u) | ((uint32_t)(x) << 24)))
