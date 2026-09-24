#include "fb.h"

#include "arch/x86/cpu/paging.h"
#include "arch/arch.h"
#include "kernel/device/device.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/mman.h"
#include "common/fb/fb_ioctl.h"
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

/**
 * device mmap op: map the linear framebuffer into a process at the address the
 * mmap syscall reserved. this is the FB6 path that replaces the interim
 * SYS_fbmap: the LFB's physical frames are shared with the kernel mapping, so
 * this just adds a user alias in the process page directory. eager, no demand
 * paging; the frames are device memory (outside PMM) so teardown skips them.
 */
static int32_t fb_dev_mmap(device_t *dev, uint32_t page_directory, uint32_t virt,
                           uint32_t length, int prot, uint32_t offset)
{
    (void)dev;

    if (!fb_ready)
    {
        return -19; /* ENODEV */
    }

    uint32_t fb_bytes = fb.height * fb.pitch;

    // clamp the request to the framebuffer; reject an offset past the end.
    if (offset >= fb_bytes)
    {
        return -22; /* EINVAL */
    }
    if (length > fb_bytes - offset)
    {
        length = fb_bytes - offset;
    }

    // writable unless the caller asked for a read-only mapping.
    uint32_t flags = (prot & PROT_WRITE) ? MMU_USER_RW : MMU_FLAG_PRESENT | MMU_FLAG_USER;

    uint32_t phys = (uint32_t)fb.addr + offset; // identity-mapped: virt == phys
    uint32_t phys_start = phys & ~(PAGE_SIZE - 1);
    uint32_t phys_end = (phys + length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    uint32_t v = virt;
    for (uint32_t p = phys_start; p < phys_end; p += PAGE_SIZE)
    {
        if (arch_map_page(page_directory, v, p, flags) != 0)
        {
            return -12; /* ENOMEM */
        }
        v += PAGE_SIZE;
    }

    return 0;
}

/**
 * device size op: total addressable framebuffer bytes.
 */
static uint32_t fb_dev_size(device_t *dev)
{
    (void)dev;
    return fb_ready ? fb.height * fb.pitch : 0;
}

/**
 * device read op: copy framebuffer bytes out at an offset (so `cat /dev/fb0`
 * dumps the screen). reads from the visible LFB, clamped to the fb size.
 */
static uint32_t fb_dev_read(device_t *dev, uint32_t offset, void *buf, uint32_t len)
{
    (void)dev;

    if (!fb_ready)
    {
        return 0;
    }

    uint32_t total = fb.height * fb.pitch;
    if (offset >= total)
    {
        return 0; // at or past end
    }
    if (len > total - offset)
    {
        len = total - offset;
    }

    memcpy(buf, fb.addr + offset, len);
    return len;
}

/**
 * device write op: copy bytes into the framebuffer at an offset. writes go to
 * the visible LFB (this is the raw fbdev path; the console back buffer is a
 * separate concern). clamped to the fb size.
 */
static uint32_t fb_dev_write(device_t *dev, uint32_t offset, const void *buf, uint32_t len)
{
    (void)dev;

    if (!fb_ready)
    {
        return 0;
    }

    uint32_t total = fb.height * fb.pitch;
    if (offset >= total)
    {
        return 0;
    }
    if (len > total - offset)
    {
        len = total - offset;
    }

    memcpy(fb.addr + offset, buf, len);
    return len;
}

/**
 * fill an fb_bitfield_t from a channel size/shift pair.
 */
static void fb_fill_bitfield(fb_bitfield_t *bf, uint8_t size, uint8_t shift)
{
    bf->offset = shift;
    bf->length = size;
    bf->msb_right = 0;
}

/**
 * device ioctl op: the Linux-style fbdev screeninfo requests.
 * GET_FSCREENINFO / GET_VSCREENINFO fill the caller's struct from fb_info;
 * PUT_VSCREENINFO validates against the single fixed mode (no mode-setting yet).
 */
static int32_t fb_dev_ioctl(device_t *dev, uint32_t request, void *arg)
{
    (void)dev;

    if (!fb_ready)
    {
        return -19; /* ENODEV */
    }

    switch (request)
    {
    case FBIOGET_FSCREENINFO:
    {
        fb_fix_screeninfo_t *fix = (fb_fix_screeninfo_t *)arg;
        memset(fix, 0, sizeof(*fix));
        strncpy(fix->id, "ginnfb", sizeof(fix->id) - 1);
        fix->smem_start = (uint32_t)fb.addr;
        fix->smem_len = fb.height * fb.pitch;
        fix->type = FB_TYPE_PACKED_PIXELS;
        fix->visual = FB_VISUAL_TRUECOLOR;
        fix->line_length = fb.pitch;
        return 0;
    }

    case FBIOGET_VSCREENINFO:
    {
        fb_var_screeninfo_t *var = (fb_var_screeninfo_t *)arg;
        memset(var, 0, sizeof(*var));
        var->xres = fb.width;
        var->yres = fb.height;
        var->xres_virtual = fb.width;
        var->yres_virtual = fb.height;
        var->bits_per_pixel = fb.bpp;
        fb_fill_bitfield(&var->red, fb.red_size, fb.red_shift);
        fb_fill_bitfield(&var->green, fb.green_size, fb.green_shift);
        fb_fill_bitfield(&var->blue, fb.blue_size, fb.blue_shift);
        fb_fill_bitfield(&var->transp, 0, 0); // no alpha
        return 0;
    }

    case FBIOPUT_VSCREENINFO:
    {
        // mode-setting is not supported: accept only a request matching the
        // current mode, reject anything else. the seam is here for later.
        const fb_var_screeninfo_t *var = (const fb_var_screeninfo_t *)arg;
        if (var->xres == fb.width && var->yres == fb.height &&
            var->bits_per_pixel == fb.bpp)
        {
            return 0;
        }
        return -22; /* EINVAL */
    }

    default:
        return -25; /* ENOTTY: unknown request */
    }
}

// device ops for fb0: the Linux-style fbdev surface. mmap for zero-copy
// drawing; read/write so `cat /dev/fb0` works; ioctl for fix/var screeninfo.
static const device_ops_t fb_device_ops = {
    .mmap = fb_dev_mmap,
    .read = fb_dev_read,
    .write = fb_dev_write,
    .size = fb_dev_size,
    .ioctl = fb_dev_ioctl,
};

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
    fb_device.ops = &fb_device_ops;
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
