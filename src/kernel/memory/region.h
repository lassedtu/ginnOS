#pragma once

#include "common/stdint.h"
#include "kernel/memory/phys.h"

/**
 * maximum number of reserved memory regions the table can hold.
 */
#define REGION_TABLE_MAX 16

/**
 * a single reserved memory region with byte-granularity boundaries.
 */
typedef struct
{
    phys_addr_t start;   // first byte of the reserved region (inclusive).
    phys_addr_t end;     // first byte past the reserved region (exclusive).
    const char *label;   // human-readable name (e.g. "kernel", "stage2").

} region_entry_t;

/**
 * reserve a memory region by recording it in the global table.
 * panics if the table is full.
 * @param start first byte of the region (inclusive).
 * @param end first byte past the region (exclusive).
 * @param label human-readable name for debug output.
 */
void region_reserve(phys_addr_t start, phys_addr_t end, const char *label);

/**
 * check whether a single address falls inside any reserved region.
 * @param address the physical address to test.
 * @return true if the address is reserved, false otherwise.
 */
bool region_is_reserved(phys_addr_t address);

/**
 * check whether a range overlaps any reserved region.
 * @param start first byte of the range (inclusive).
 * @param end first byte past the range (exclusive).
 * @return true if any overlap exists, false otherwise.
 */
bool region_overlaps(phys_addr_t start, phys_addr_t end);

/**
 * return the number of currently reserved regions.
 */
uint32_t region_count(void);

/**
 * print all reserved regions to the console.
 */
void region_print_all(void);
