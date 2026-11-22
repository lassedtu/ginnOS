#pragma once

#include "common/stdint.h"
#include "memory_map.h"

/**
 * information the bootloader hands to the kernel.
 *
 * the layout up to and including memory_map is shared byte-for-byte with the
 * stage2 assembly that fills it in (see src/bootloader/stage2/main.asm), so
 * those fields must not be reordered. anything a boot protocol cannot supply
 * is left zero: framebuffer_addr == 0 means "no framebuffer", cmdline_addr
 * == 0 means "no command line".
 *
 * stage2 fills in the fields up to and including the framebuffer pixel
 * format. the kernel copies only that part into its own boot_info_t
 * (kernel.c), so the fields after it are zero on a stage2 boot. a multiboot 2
 * boot fills in all fields (arch/x86/boot/multiboot2.c).
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
    uint32_t cmdline_addr;       // physical address of a NUL-terminated boot command
                                 // line, or 0 if none (a fixed-width field: stage2
                                 // is 32-bit, the kernel may not be)

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

    // the fields below are not shared with stage2

    uint32_t boot_protocol; // BOOT_PROTOCOL_STAGE2, _MULTIBOOT2 or _LIMINE.

    // a root filesystem image that the bootloader loaded into RAM (a boot
    // module), or 0 and 0. the physical range [initrd_start, initrd_end)
    // stays reserved.
    uint32_t initrd_start;
    uint32_t initrd_end;

    // a copy of the ACPI RSDP from the bootloader (rsdp_size 0: none). a
    // multiboot 2 bootloader gives a copy, not an address.
    uint32_t rsdp_size;
    uint8_t rsdp[36];

    // bits 32 to 63 of the framebuffer address: on UEFI machines it is
    // often above 4 GiB (only a 64-bit kernel can use that)
    uint32_t framebuffer_addr_high;
} boot_info_t;

#define BOOT_PROTOCOL_STAGE2     0 // the custom BIOS bootloader (src/bootloader)
#define BOOT_PROTOCOL_MULTIBOOT2 1 // a multiboot 2 bootloader, for example limine
#define BOOT_PROTOCOL_LIMINE     2 // the limine boot protocol (x86_64)

/* the part of boot_info_t that stage2 fills in */
#define BOOT_INFO_STAGE2_SIZE __builtin_offsetof(boot_info_t, _fb_pad)