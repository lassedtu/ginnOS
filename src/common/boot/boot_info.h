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

} boot_info_t;