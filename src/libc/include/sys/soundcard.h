#pragma once

/**
 * @file sys/soundcard.h
 * @brief the OSS sound interface: ioctl numbers and formats (ABI).
 *
 * the numbers are the ones of the Open Sound System on Linux, so ported
 * programs find the names they know. the kernel has a copy in
 * src/kernel/audio/soundcard.h; the two must stay the same.
 */

#include <stdint.h>

// sample formats
#define AFMT_U8     0x00000008
#define AFMT_S16_LE 0x00000010

// /dev/dsp
#define SNDCTL_DSP_RESET     0x00005000u // stop and drop the queued sound
#define SNDCTL_DSP_SYNC      0x00005001u // wait until the queued sound has played
#define SNDCTL_DSP_SPEED     0xC0045002u // int: the sample rate (the device's nearest back)
#define SNDCTL_DSP_STEREO    0xC0045003u // int: 0 mono, 1 stereo
#define SNDCTL_DSP_GETBLKSIZE 0xC0045004u // int: the fragment size
#define SNDCTL_DSP_SETFMT    0xC0045005u // int: AFMT_ (the format in use back)
#define SNDCTL_DSP_CHANNELS  0xC0045006u // int: 1 or 2
#define SNDCTL_DSP_GETFMTS   0x8004500Bu // int: the formats the device takes
#define SNDCTL_DSP_GETOSPACE 0x8010500Cu // audio_buf_info: free space
#define SNDCTL_DSP_GETODELAY 0x80045017u // int: bytes queued, not played yet

/**
 * the free space for SNDCTL_DSP_GETOSPACE, in bytes of the caller's format.
 */
typedef struct
{
    int32_t fragments;  // whole free fragments
    int32_t fragstotal; // fragments in the buffer
    int32_t fragsize;   // bytes per fragment
    int32_t bytes;      // free bytes
} audio_buf_info; // NOLINT(readability-identifier-naming): the OSS name

// /dev/mixer: a volume is left | right << 8, each 0 to 100
#define SOUND_MIXER_READ_VOLUME  0x80044D00u
#define SOUND_MIXER_WRITE_VOLUME 0xC0044D00u
#define SOUND_MIXER_READ_DEVMASK 0x80044DFEu
#define SOUND_MASK_VOLUME        0x00000001
