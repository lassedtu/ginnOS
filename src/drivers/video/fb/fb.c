#include "fb.h"

#include "arch/x86/cpu/paging.h"
#include "kernel/device/device.h"
#include "kernel/memory/heap.h"
#include "common/memory.h"
#include "common/string.h"

/**
 * @file fb.c
 * @brief linear framebuffer driver implementation.
 *
 * drawing is double-buffered: ops render into a RAM back buffer and mark the
 * touched scanlines dirty; fb_flush() copies just those rows out to the slow,
 * uncached linear framebuffer. if the back buffer can't be allocated the
 * driver falls back to writing the LFB directly (draw_target == fb.addr) and
 * fb_flush becomes a no-op.
 */

static fb_info_t fb;
static bool fb_ready;

// where drawing lands: the back buffer when double-buffered, else the LFB.
static uint8_t *draw_target;
static uint8_t *back_buffer;

// dirty scanline range [dirty_top, dirty_bottom) pending flush; empty when
// dirty_top >= dirty_bottom.
static uint32_t dirty_top;
static uint32_t dirty_bottom;

/**
 * mark scanlines [y, y+h) as needing a flush to the visible framebuffer.
 */
static void mark_dirty(uint32_t y, uint32_t h)
{
    if (!back_buffer)
    {
        return;
    }
    uint32_t bottom = y + h;
    if (bottom > fb.height)
    {
        bottom = fb.height;
    }
    if (dirty_top >= dirty_bottom)
    {
        dirty_top = y;
        dirty_bottom = bottom;
    }
    else
    {
        if (y < dirty_top)
        {
            dirty_top = y;
        }
        if (bottom > dirty_bottom)
        {
            dirty_bottom = bottom;
        }
    }
}

/**
 * map the framebuffer's physical range into the kernel address space.
 * the LFB sits above identity-mapped RAM, so its pages are not present yet;
 * paging_map allocates page tables as needed. virtual == physical keeps the
 * driver's addressing trivial.
 */
static void map_framebuffer(uint32_t phys_base, uint32_t bytes)
{
    uint32_t start = phys_base & ~(PAGE_SIZE - 1);
    uint32_t end = (phys_base + bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE)
    {
        paging_map(addr, addr, PTE_KERNEL_RW);
    }
}

bool fb_init(const boot_info_t *boot)
{
    if (!boot || boot->framebuffer_type == 0 || boot->framebuffer_addr == 0)
    {
        fb_ready = false;
        return false;
    }

    fb.addr = (uint8_t *)boot->framebuffer_addr;
    fb.width = boot->framebuffer_width;
    fb.height = boot->framebuffer_height;
    fb.pitch = boot->framebuffer_pitch;
    fb.bpp = boot->framebuffer_bpp;
    fb.bytes_pp = (uint8_t)(boot->framebuffer_bpp / 8);
    fb.red_size = boot->framebuffer_red_size;
    fb.red_shift = boot->framebuffer_red_shift;
    fb.green_size = boot->framebuffer_green_size;
    fb.green_shift = boot->framebuffer_green_shift;
    fb.blue_size = boot->framebuffer_blue_size;
    fb.blue_shift = boot->framebuffer_blue_shift;

    // only 24- and 32-bpp direct colour are supported (what FB0 selects).
    if (fb.bytes_pp != 3 && fb.bytes_pp != 4)
    {
        fb_ready = false;
        return false;
    }

    map_framebuffer(boot->framebuffer_addr, fb.height * fb.pitch);

    // try to allocate a RAM back buffer for double buffering. on failure we
    // simply draw straight to the LFB (draw_target == fb.addr).
    uint32_t fb_bytes = fb.height * fb.pitch;
    back_buffer = (uint8_t *)kmalloc(fb_bytes);
    draw_target = back_buffer ? back_buffer : fb.addr;
    dirty_top = 0;
    dirty_bottom = 0;

    fb_ready = true;

    // register with the device model.
    static device_t fb_device;
    strncpy(fb_device.name, "fb0", DEVICE_NAME_MAX - 1);
    fb_device.name[DEVICE_NAME_MAX - 1] = '\0';
    fb_device.type = DEVICE_TYPE_CHAR;
    fb_device.ops = 0;
    fb_device.driver_data = &fb;
    device_register(&fb_device);

    return true;
}

bool fb_available(void)
{
    return fb_ready;
}

const fb_info_t *fb_get_info(void)
{
    return &fb;
}

/**
 * scale an 8-bit channel value down to a field of the given width, then shift
 * it into place. for 8-bit channels this is just a shift.
 */
static uint32_t pack_channel(uint8_t value, uint8_t size, uint8_t shift)
{
    if (size >= 8)
    {
        return (uint32_t)value << shift;
    }
    // narrow the 8-bit value to the field width (e.g. 5- or 6-bit channels).
    return (uint32_t)(value >> (8 - size)) << shift;
}

uint32_t fb_pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return pack_channel(r, fb.red_size, fb.red_shift) |
           pack_channel(g, fb.green_size, fb.green_shift) |
           pack_channel(b, fb.blue_size, fb.blue_shift);
}

/**
 * store a packed pixel at a byte offset, writing bytes_pp bytes.
 */
static inline void store_pixel(uint32_t offset, uint32_t pixel)
{
    if (fb.bytes_pp == 4)
    {
        *(uint32_t *)(draw_target + offset) = pixel;
    }
    else
    {
        // 24-bpp: write three bytes, low-to-high.
        draw_target[offset + 0] = (uint8_t)(pixel & 0xFF);
        draw_target[offset + 1] = (uint8_t)((pixel >> 8) & 0xFF);
        draw_target[offset + 2] = (uint8_t)((pixel >> 16) & 0xFF);
    }
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t pixel)
{
    if (!fb_ready || x >= fb.width || y >= fb.height)
    {
        return;
    }

    store_pixel(y * fb.pitch + x * fb.bytes_pp, pixel);
    mark_dirty(y, 1);
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t pixel)
{
    if (!fb_ready)
    {
        return;
    }

    // clip to the framebuffer.
    if (x >= fb.width || y >= fb.height)
    {
        return;
    }
    if (x + w > fb.width)
    {
        w = fb.width - x;
    }
    if (y + h > fb.height)
    {
        h = fb.height - y;
    }

    for (uint32_t row = 0; row < h; row++)
    {
        uint32_t offset = (y + row) * fb.pitch + x * fb.bytes_pp;
        for (uint32_t col = 0; col < w; col++)
        {
            store_pixel(offset, pixel);
            offset += fb.bytes_pp;
        }
    }

    mark_dirty(y, h);
}

void fb_clear(uint32_t pixel)
{
    if (!fb_ready)
    {
        return;
    }

    fb_fill_rect(0, 0, fb.width, fb.height, pixel);
}

void fb_copy_rect(uint32_t dst_x, uint32_t dst_y,
                  uint32_t src_x, uint32_t src_y,
                  uint32_t w, uint32_t h)
{
    if (!fb_ready)
    {
        return;
    }

    // top-to-bottom row order is safe when moving up (dst_y < src_y), which is
    // how the terminal scrolls.
    for (uint32_t row = 0; row < h; row++)
    {
        uint8_t *dst = draw_target + (dst_y + row) * fb.pitch + dst_x * fb.bytes_pp;
        uint8_t *src = draw_target + (src_y + row) * fb.pitch + src_x * fb.bytes_pp;
        memcpy(dst, src, w * fb.bytes_pp);
    }

    mark_dirty(dst_y, h);
}

void fb_flush(void)
{
    if (!fb_ready || !back_buffer || dirty_top >= dirty_bottom)
    {
        return;
    }

    // copy the dirty scanline span from the back buffer to the LFB in one go.
    uint32_t offset = dirty_top * fb.pitch;
    uint32_t bytes = (dirty_bottom - dirty_top) * fb.pitch;
    memcpy(fb.addr + offset, back_buffer + offset, bytes);

    dirty_top = 0;
    dirty_bottom = 0;
}
