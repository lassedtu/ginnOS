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
 * FBIO_CURSOR: arg is an fb_cursor_t. a display that has a pointer of its
 * own (virtio-gpu) shows pixels over the screen, with their top left
 * corner at x, y; shown 0 hides it. only the owner of the screen
 * (FBIO_GRAPHICS) can use it, and the pointer goes when the owner gives
 * the screen back or ends. ENOTTY when the display has no pointer: draw
 * it in the picture then.
 *
 * FBIO_CURSOR_MOVE: arg is an fb_point_t, the new top left corner.
 */
#define FB_CURSOR_SIZE 64

typedef struct
{
    uint32_t shown;
    uint32_t x;
    uint32_t y;
    uint32_t pixels[FB_CURSOR_SIZE * FB_CURSOR_SIZE]; // a8r8g8b8, row after row
} fb_cursor_t;

typedef struct
{
    uint32_t x;
    uint32_t y;
} fb_point_t;

#define FBIO_CURSOR      0x46F3
#define FBIO_CURSOR_MOVE 0x46F4

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

/**
 * open the framebuffer device (/dev/fb0).
 * @return an open fd on success, or -1 on error (errno set).
 */
int fb_open(void);
