#include "syscall_internal.h"
#include "syscall.h"

#include "drivers/video/fb/fb.h"
#include "drivers/keyboard/keyboard.h"
#include "kernel/process/process.h"
#include "kernel/memory/pmm_layout.h"
#include "arch/arch.h"

/**
 * @file sys_fb.c
 * @brief framebuffer syscalls: query geometry and map the framebuffer.
 *
 * this is the interim single-client path toward visual programs. the long-term
 * design is a compositor owning /dev/fb0 and handing clients off-screen
 * buffers; until then a program maps the linear framebuffer directly.
 */

/**
 * userspace-facing framebuffer descriptor. layout must match fb_info_t in
 * src/libc/include/fb.h. kept as a local definition because the libc header is
 * not on the kernel include path.
 */
typedef struct
{
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint8_t red_size, red_shift;
    uint8_t green_size, green_shift;
    uint8_t blue_size, blue_shift;
    uint8_t _pad[2];
} user_fb_info_t;

/**
 * SYS_fbinfo: copy the framebuffer geometry/format into a user struct.
 * arg: EBX = pointer to a user_fb_info_t.
 * returns 0 on success, -1 if there is no framebuffer, -EFAULT on bad pointer.
 */
int32_t sys_fbinfo(struct registers *regs)
{
    user_fb_info_t *out = (user_fb_info_t *)regs->ebx;

    if (!is_user_ptr(out, sizeof(*out)))
    {
        return -18; /* EFAULT */
    }

    if (!fb_available())
    {
        return -1;
    }

    const fb_info_t *info = fb_get_info();
    out->width = info->width;
    out->height = info->height;
    out->pitch = info->pitch;
    out->bpp = info->bpp;
    out->red_size = info->red_size;
    out->red_shift = info->red_shift;
    out->green_size = info->green_size;
    out->green_shift = info->green_shift;
    out->blue_size = info->blue_size;
    out->blue_shift = info->blue_shift;
    out->_pad[0] = 0;
    out->_pad[1] = 0;
    return 0;
}

/**
 * SYS_fbmap: map the linear framebuffer into the calling process at a fixed
 * userspace address, user-writable, and return that address.
 * returns the map address on success, 0 on failure.
 *
 * the framebuffer's physical frames are shared with the kernel's own mapping;
 * this just adds a user-accessible alias in the process page directory.
 */
int32_t sys_fbmap(struct registers *regs)
{
    (void)regs;

    if (!fb_available())
    {
        return 0;
    }

    process_t *proc = process_current();
    if (!proc)
    {
        return 0;
    }

    const fb_info_t *info = fb_get_info();
    uint32_t phys = (uint32_t)info->addr; // identity-mapped: virt == phys
    uint32_t bytes = info->height * info->pitch;
    uint32_t phys_start = phys & ~(PAGE_SIZE - 1);
    uint32_t phys_end = (phys + bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // map each framebuffer page into the process at USER_FB_MAP_ADDR, keeping
    // the same page offset within the mapping as the physical layout.
    uint32_t virt = USER_FB_MAP_ADDR;
    for (uint32_t p = phys_start; p < phys_end; p += PAGE_SIZE)
    {
        if (arch_map_page(proc->page_directory, virt, p, MMU_USER_RW) != 0)
        {
            return 0;
        }
        virt += PAGE_SIZE;
    }

    return (int32_t)USER_FB_MAP_ADDR;
}

/**
 * SYS_pollkey: non-blocking keyboard poll for interactive graphics programs.
 * returns the next pending character-key (a "kbhit"-style peek+consume), or 0
 * if no key is waiting. special keys (arrows etc.) are consumed and reported
 * as 0 so they don't stall the caller.
 * takes no arguments.
 */
int32_t sys_pollkey(struct registers *regs)
{
    (void)regs;

    if (!keyboard_available())
    {
        return 0;
    }

    // a key is ready, so this does not block.
    char c = keyboard_getchar();
    return (int32_t)(unsigned char)c;
}
