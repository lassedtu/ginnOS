#pragma once

#include "common/stdint.h"

/**
 * structure representing a single memory region in the system's memory map.
 */
typedef struct
{
    uint64_t base;   // starting physical address of the memory region.
    uint64_t length; // length of the memory region in bytes.
    uint32_t type;   // type of the memory region (1 = usable, 2 = reserved, etc.).

} memory_region_t;

/**
 * structure representing the system's memory map, containing multiple memory regions.
 */
typedef struct
{
    uint32_t count;              // number of valid memory regions in the regions array.
    memory_region_t regions[32]; // array of memory regions, with a maximum of 32 entries.

} memory_map_t;