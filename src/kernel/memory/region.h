#pragma once

#include "../../common/stdint.h"

/**
 * maximum number of reserved memory regions the table can hold.
 */
#define REGION_TABLE_MAX 16

/**
 * a single reserved memory region with byte-granularity boundaries.
 */
typedef struct
{
    uint32_t start;      // first byte of the reserved region (inclusive).
    uint32_t end;        // first byte past the reserved region (exclusive).
    const char *label;   // human-readable name (e.g. "kernel", "stage2").

} region_entry_t;

/**
 * reserve a memory region by recording it in the global table.
 * panics if the table is full.
 * @param start first byte of the region (inclusive).
 * @param end first byte past the region (exclusive).
 * @param label human-readable name for debug output.
 */
void region_reserve(uint32_t start, uint32_t end, const char *label);

/**
 * check whether a single address falls inside any reserved region.
 * @param address the physical address to test.
 * @return true if the address is reserved, false otherwise.
 */
bool region_is_reserved(uint32_t address);

/**
 * check whether a range overlaps any reserved region.
 * @param start first byte of the range (inclusive).
 * @param end first byte past the range (exclusive).
 * @return true if any overlap exists, false otherwise.
 */
bool region_overlaps(uint32_t start, uint32_t end);

/**
 * return the number of currently reserved regions.
 */
uint32_t region_count(void);

/**
 * print all reserved regions to the console.
 */
void region_print_all(void);
