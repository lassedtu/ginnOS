#pragma once

/**
 * @file fb_ioctl.h
 * @brief framebuffer device uapi: screeninfo structs and FBIO* request codes.
 *
 * modelled on Linux's <linux/fb.h>, trimmed to what ginnOS supports: 32-bit
 * truecolor modes, packed pixels, no colormap, no CRTC timings. these
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

/**
 * FBIOGET_MODE: one video mode. the caller sets index (0, 1, ...); the
 * kernel fills in the rest, or fails with EINVAL past the last mode. with
 * a display driver the list is the sizes it can set, the preferred one
 * first; without one it is the firmware's list from the bootloader (or
 * only the current mode).
 */
typedef struct
{
    uint32_t index;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t current;   // 1 for the mode in use
    uint32_t preferred; // 1 for the size the screen wants (the QEMU window)
} fb_mode_t;

#define FBIOGET_MODE 0x46F0

/**
 * FBIO_GRAPHICS: int arg 1 takes the screen for this process, 0 gives it
 * back. while a process has the screen, the text console does not draw on
 * it (its text is kept, and comes back when the screen is given back). the
 * screen comes back by itself when the process ends, and on a kernel
 * panic. EBUSY if another live process has it.
 */
#define FBIO_GRAPHICS 0x46F1

/**
 * FBIO_FLUSH: arg is an fb_rect_t of pixels that changed. a display that
 * does not show the memory by itself (virtio-gpu) copies them to the
 * screen; for the others it does nothing. a mapped framebuffer is only
 * sure to be seen after this.
 */
typedef struct
{
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} fb_rect_t;

#define FBIO_FLUSH 0x46F2

/**
 * FBIOPUT_VSCREENINFO sets the mode to xres x yres at 32 bits per pixel
 * when a display driver can (EINVAL if it cannot). the framebuffer can
 * move: map it again after a change.
 *
 * /dev/display: read gives fb_event_t records; poll says when one is
 * there. a record comes when the mode changed and when the size the
 * screen wants changed (the QEMU window was resized).
 */
typedef struct
{
    uint32_t type; // FB_EVENT_*
    uint32_t width;
    uint32_t height;
} fb_event_t;

#define FB_EVENT_MODE      1 // the mode is now width x height
#define FB_EVENT_PREFERRED 2 // the screen wants width x height
