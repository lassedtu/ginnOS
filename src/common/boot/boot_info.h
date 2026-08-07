#pragma once

#include "../stdint.h"
#include "memory_map.h"

/**
 * structure containing information passed from the bootloader to the kernel.
 */
typedef struct
{
    uint8_t boot_drive;      // the BIOS drive number of the boot drive (e.g., 0x80 for the first hard disk).
    uint8_t _reserved[3];    // reserved bytes for alignment, should be set to zero.
    memory_map_t memory_map; // the memory map provided by the bootloader, describing available and reserved memory regions.

} boot_info_t;