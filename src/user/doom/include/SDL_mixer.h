#pragma once

/**
 * @file SDL_mixer.h
 * @brief an empty stand-in for SDL_mixer.
 *
 * doomgeneric's i_sound.c includes <SDL_mixer.h> when FEATURE_SOUND is on,
 * but uses nothing from it. ginnOS has no SDL; the sound goes through the
 * ginnOS sound module (doomgeneric_ginnos_sound.c) instead. this file lets
 * the engine build without a change to it.
 */
