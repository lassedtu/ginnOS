#pragma once

/**
 * @file fb.h
 * @brief linear framebuffer driver.
 *
 * a pixel-pushing driver for the VBE linear framebuffer described in
 * boot_info_t (see the FB0 bootloader work). it is the framebuffer equivalent
 * of the raw VGA text driver: it knows nothing about fonts, terminals, or ANSI
 * only pixels and rectangles. higher layers (the font renderer, the tty
 * backend) build on top of it.
 *
 * pixels are composed from the channel masks the boot protocol reported, so
 * the driver works for whatever RGB packing the mode uses (24- or 32-bpp)
 * rather than assuming a fixed 0x00RRGGBB layout.
 */

#include "common/stdint.h"
#include "common/boot/boot_info.h"

/**
 * framebuffer geometry and pixel format, filled by fb_init().
 */
typedef struct
{
    uint8_t *addr;    // framebuffer base (virtual == physical, identity-mapped).
    uint32_t width;   // width in pixels.
    uint32_t height;  // height in pixels.
    uint32_t pitch;   // bytes per scanline (may exceed width * bytes_per_pixel).
    uint8_t bpp;      // bits per pixel (24 or 32).
    uint8_t bytes_pp; // bytes per pixel (3 or 4).
    uint8_t red_size, red_shift;
    uint8_t green_size, green_shift;
    uint8_t blue_size, blue_shift;
} fb_info_t;

/**
 * initialize the framebuffer from the boot info and map it into the address
 * space. must be called after paging_init(), since the framebuffer lives at a
 * high physical address outside the identity-mapped RAM range.
 * @param boot the boot information from the bootloader.
 * @return true if a usable framebuffer was set up, false if none is present.
 */
bool fb_init(const boot_info_t *boot);

/**
 * whether fb_init() found and mapped a framebuffer.
 */
bool fb_available(void);

/**
 * read-only view of the framebuffer geometry/format. valid after fb_init()
 * returns true.
 */
const fb_info_t *fb_get_info(void);

/**
 * compose a pixel value from 8-bit r/g/b according to the framebuffer masks.
 * @return the packed pixel, ready to store at a pixel address.
 */
uint32_t fb_pack_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * write one pixel. out-of-bounds coordinates are ignored.
 * @param x column, @param y row, @param pixel packed pixel from fb_pack_rgb().
 */
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t pixel);

/**
 * fill a rectangle with a packed pixel value. clipped to the framebuffer.
 */
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t pixel);

/**
 * clear the whole framebuffer to a packed pixel value.
 */
void fb_clear(uint32_t pixel);

/**
 * copy a rectangle from one position to another (used for scrolling).
 * source and destination may overlap vertically; rows are copied in an order
 * that is safe for an upward move (dst_y < src_y).
 */
void fb_copy_rect(uint32_t dst_x, uint32_t dst_y, uint32_t src_x, uint32_t src_y, uint32_t w,
                  uint32_t h);
