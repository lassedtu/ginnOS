#pragma once

/**
 * @file fb.h
 * @brief userspace access to the linear framebuffer.
 *
 * a program calls fbinfo() to learn the framebuffer geometry and pixel format,
 * then fbmap() to map it into its address space and draw pixels directly.
 * these are convenience wrappers over the /dev/fb0 fbdev (open + ioctl + mmap,
 * see <sys/fb.h>); a program can use that interface directly instead. this is
 * still a single-client path (no compositor yet): whoever maps it owns the
 * whole screen until it exits.
 */

typedef unsigned int size_t;
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

/**
 * framebuffer geometry and pixel format, filled by fbinfo().
 * mirrors the kernel's view; the channel size/shift pairs describe how r/g/b
 * sit inside a pixel so a program can pack colours for any layout.
 */
typedef struct
{
    uint32_t width;   // width in pixels.
    uint32_t height;  // height in pixels.
    uint32_t pitch;   // bytes per scanline (may exceed width * bpp/8).
    uint32_t bpp;     // bits per pixel (24 or 32).
    uint8_t red_size, red_shift;
    uint8_t green_size, green_shift;
    uint8_t blue_size, blue_shift;
    uint8_t _pad[2];
} fb_info_t;

/**
 * query the framebuffer geometry and pixel format.
 * @param info output struct.
 * @return 0 on success, -1 if there is no framebuffer.
 */
int fbinfo(fb_info_t *info);

/**
 * map the framebuffer into this process's address space.
 * @return a writable pointer to the framebuffer, or NULL on failure. the
 *         returned buffer is `pitch * height` bytes; write packed pixels.
 */
void *fbmap(void);

/**
 * non-blocking keyboard poll for interactive graphics programs.
 * @return the next pending character key, or 0 if none is waiting.
 */
int pollkey(void);
