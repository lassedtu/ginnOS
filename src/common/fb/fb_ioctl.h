#pragma once

/**
 * @file fb_ioctl.h
 * @brief framebuffer device uapi: screeninfo structs and FBIO* request codes.
 *
 * modelled on Linux's <linux/fb.h>, trimmed to what ginnOS supports: a single
 * fixed truecolor VBE mode, packed pixels, no colormap, no CRTC timings. these
 * definitions are ABI and must match the libc mirror in
 * src/libc/include/sys/fb.h exactly, since userspace passes them across the
 * ioctl boundary.
 */

#include "common/stdint.h"

// how a single colour channel sits inside a pixel. offset is the bit position
// of the least-significant bit; length is the number of bits. msb_right is 0
// for the usual little-endian channel order (kept for Linux source-compat).
typedef struct
{
    uint32_t offset;    // bit position of the channel's LSB
    uint32_t length;    // number of bits
    uint32_t msb_right; // != 0 if the MSB is on the right (always 0 here)
} fb_bitfield_t;

// fixed screen info: facts that do not change for the life of the mode.
typedef struct
{
    char id[16];          // identification string, e.g. "ginnfb"
    uint32_t smem_start;  // framebuffer physical address
    uint32_t smem_len;    // framebuffer length in bytes
    uint32_t type;        // FB_TYPE_* (packed pixels)
    uint32_t visual;      // FB_VISUAL_* (truecolor)
    uint32_t line_length; // bytes per scanline (pitch)
} fb_fix_screeninfo_t;

// variable screen info: geometry and pixel format.
typedef struct
{
    uint32_t xres;         // visible resolution, width
    uint32_t yres;         // visible resolution, height
    uint32_t xres_virtual; // virtual resolution, width (== xres here)
    uint32_t yres_virtual; // virtual resolution, height (== yres here)
    uint32_t bits_per_pixel;
    fb_bitfield_t red;    // bit layout of the red channel
    fb_bitfield_t green;  // bit layout of the green channel
    fb_bitfield_t blue;   // bit layout of the blue channel
    fb_bitfield_t transp; // bit layout of the alpha channel (length 0 if none)
} fb_var_screeninfo_t;

// fb_fix_screeninfo.type values.
#define FB_TYPE_PACKED_PIXELS 0

// fb_fix_screeninfo.visual values.
#define FB_VISUAL_TRUECOLOR 2

// ioctl request codes. simple small integers (no Linux _IOR encoding).
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
