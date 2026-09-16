#pragma once

#include "common/stdint.h"
#include "memory_map.h"

/**
 * information the bootloader hands to the kernel.
 *
 * the layout up to and including memory_map is shared byte-for-byte with the
 * stage2 assembly that fills it in (see src/bootloader/stage2/main.asm), so
 * those fields must not be reordered. anything a boot protocol cannot supply
 * is left zero: framebuffer_addr == 0 means "no framebuffer", cmdline == NULL
 * means "no command line". the custom BIOS bootloader zeroes these; a future
 * GRUB/UEFI path fills them in.
 */
typedef struct
{
    // shared with stage2 assembly (do not reorder)
    uint8_t boot_drive;      // x86/BIOS drive number the system booted from (e.g. 0x80).
    uint8_t _reserved[3];    // alignment padding, zeroed.
    memory_map_t memory_map; // physical memory regions reported by the boot protocol.

    // arch-neutral, filled when the boot protocol provides them
    uint32_t framebuffer_addr;   // physical address of a linear framebuffer, or 0 if none.
    uint32_t framebuffer_width;  // framebuffer width in pixels (valid when addr != 0).
    uint32_t framebuffer_height; // framebuffer height in pixels (valid when addr != 0).
    const char *cmdline;         // NUL-terminated boot command line, or NULL if none.

    // framebuffer pixel format (valid when framebuffer_type != 0). the channel
    // size/shift pairs describe how r/g/b sit inside a pixel, so the driver can
    // compose pixels for whatever layout VBE reports instead of assuming
    // 0x00RRGGBB. all zero when there is no framebuffer.
    uint32_t framebuffer_pitch;      // bytes per scanline (may exceed width * bpp/8).
    uint8_t framebuffer_bpp;         // bits per pixel (e.g. 32).
    uint8_t framebuffer_type;        // 0 = none/text, 1 = RGB linear framebuffer.
    uint8_t framebuffer_red_size;    // red channel bit width.
    uint8_t framebuffer_red_shift;   // red channel low bit position.
    uint8_t framebuffer_green_size;  // green channel bit width.
    uint8_t framebuffer_green_shift; // green channel low bit position.
    uint8_t framebuffer_blue_size;   // blue channel bit width.
    uint8_t framebuffer_blue_shift;  // blue channel low bit position.
    uint8_t _fb_pad[2];              // pad to a 4-byte boundary, zeroed.

} boot_info_t;