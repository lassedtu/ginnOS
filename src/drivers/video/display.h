#pragma once

/**
 * @file display.h
 * @brief display drivers: the layer between the fbdev (/dev/fb0) and a
 *        card that can change its mode.
 *
 * without a driver, the screen is the framebuffer the bootloader set up,
 * in one mode. a driver (the Bochs/QEMU VGA, virtio-gpu) can set other
 * sizes at run time, and can know the size the screen wants (the QEMU
 * window). /dev/display tells programs when the mode or that size
 * changes.
 */

#include "common/stdint.h"
#include "kernel/memory/phys.h"

/**
 * a framebuffer in a mode: 32 bits per pixel, x8r8g8b8.
 */
typedef struct
{
    phys_addr_t phys; // where it is (contiguous)
    uint8_t *addr;    // the kernel's pointer to it
    uint32_t width;
    uint32_t height;
    uint32_t pitch; // bytes per row
} display_surface_t;

typedef struct
{
    const char *name;
    uint32_t max_width; // the largest mode it can set
    uint32_t max_height;

    /**
     * set a mode.
     * @param out receives the framebuffer of the new mode.
     * @return false if the size is not possible (the old mode stays).
     */
    bool (*set_mode)(uint32_t width, uint32_t height, display_surface_t *out);

    /**
     * the size the screen wants (optional).
     * @return false if it does not know.
     */
    bool (*preferred)(uint32_t *width, uint32_t *height);

    /**
     * copy a rect of the framebuffer to the screen (optional: NULL when
     * the screen shows the memory by itself).
     */
    void (*flush)(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

    /**
     * show a pointer over the framebuffer, drawn by the card (optional).
     * @param pixels FB_CURSOR_SIZE x FB_CURSOR_SIZE a8r8g8b8, or NULL to
     *        hide the pointer.
     * @param x where its top left corner goes.
     * @return false if the card refused.
     */
    bool (*cursor_set)(const uint32_t *pixels, uint32_t x, uint32_t y);

    /**
     * move the pointer's top left corner to x, y (with cursor_set).
     */
    void (*cursor_move)(uint32_t x, uint32_t y);
} display_driver_t;

/**
 * register /dev/display. call once, before the drivers.
 */
void display_init(void);

/**
 * the driver in use, or NULL (the bootloader's framebuffer only).
 */
const display_driver_t *display_driver(void);

/**
 * use a driver from now on, and set the size the screen wants if it knows
 * one. call after fb_init.
 */
void display_attach(const display_driver_t *driver);

/**
 * a mode by index for FBIOGET_MODE: the preferred size first, then common
 * sizes the driver can set.
 * @return false past the last one.
 */
bool display_mode_at(uint32_t index, uint32_t *width, uint32_t *height, bool *preferred);

/**
 * tell the readers of /dev/display about a change.
 * @param type FB_EVENT_MODE or FB_EVENT_PREFERRED.
 */
void display_event(uint32_t type, uint32_t width, uint32_t height);

/**
 * the size the screen wants may have changed (a driver's interrupt calls
 * it). the driver is asked again in process context, when /dev/display is
 * read or polled.
 */
void display_preferred_changed(void);
