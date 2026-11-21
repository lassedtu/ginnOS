#pragma once

/**
 * page size and alignment helpers: the one definition of PAGE_SIZE.
 * the hardware page is 4 KiB on both i686 and x86_64.
 */

#include "common/stdint.h"

// bytes in a page, and log2 of that
#define PAGE_SHIFT 12u
#define PAGE_SIZE (1u << PAGE_SHIFT)

// mask of the offset-within-page bits
#define PAGE_OFFSET_MASK (PAGE_SIZE - 1u)

// round an address or size down / up to a page boundary
#define PAGE_ALIGN_DOWN(x) ((x) & ~(uintptr_t)PAGE_OFFSET_MASK)
#define PAGE_ALIGN_UP(x) (((x) + PAGE_OFFSET_MASK) & ~(uintptr_t)PAGE_OFFSET_MASK)

// true if an address or size is a whole number of pages
#define PAGE_IS_ALIGNED(x) (((x) & PAGE_OFFSET_MASK) == 0)

// number of pages needed to hold a byte count
#define PAGE_COUNT(bytes) (((bytes) + PAGE_OFFSET_MASK) >> PAGE_SHIFT)
