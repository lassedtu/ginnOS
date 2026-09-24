#pragma once

/**
 * @file sys/fb.h
 * @brief framebuffer device interface (userspace).
 *
 * the fbdev model: open "/dev/fb0", ioctl for geometry, mmap for pixels.
 * the screeninfo structs and FBIO* codes are ABI and must match the kernel in
 * src/common/fb/fb_ioctl.h exactly.
 */

typedef unsigned int uint32_t;

// how a single colour channel sits inside a pixel.
typedef struct
{
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
} fb_bitfield_t;

// fixed screen info.
typedef struct
{
    char id[16];
    uint32_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t visual;
    uint32_t line_length;
} fb_fix_screeninfo_t;

// variable screen info.
typedef struct
{
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t bits_per_pixel;
    fb_bitfield_t red;
    fb_bitfield_t green;
    fb_bitfield_t blue;
    fb_bitfield_t transp;
} fb_var_screeninfo_t;

#define FB_TYPE_PACKED_PIXELS 0
#define FB_VISUAL_TRUECOLOR   2

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602

/**
 * open the framebuffer device (/dev/fb0).
 * @return an open fd on success, or -1 on error (errno set).
 */
int fb_open(void);
