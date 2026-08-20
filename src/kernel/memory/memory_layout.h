#pragma once

#include "common/stdint.h"

/**
 * ginnOS virtual memory layout constants.
 *
 * current identity-mapped layout (no higher-half kernel yet):
 *   0x00000000 - 0x003FFFFF : kernel, bootloader, hardware (4 MiB)
 *   0x00400000 - 0x007FFFFF : reserved / unmapped
 *   0x007FC000 - 0x007FFFFF : user stack (16 KiB, 4 pages)
 *   0x00800000 - ~          : user program text/data/bss
 *   ~          - ~          : user heap (grows up via sbrk)
 *   0xC0000000 - 0xFFFFFFFF : kernel page table entries (no user access)
 *
 * user space is defined as addresses that user programs can legitimately
 * access. kernel space is everything else.
 */

/* lowest address a user pointer can validly reference (bottom of user stack) */
#define USER_SPACE_START 0x00400000u

/* one past the highest user-accessible address */
#define USER_SPACE_END   0xC0000000u

/* kernel physical load address */
#define KERNEL_PHYS_BASE 0x00010000u

/* user program load address (matches linker/user.ld) */
#define USER_LOAD_ADDR   0x00800000u

/* user stack base (just below program load) */
#define USER_STACK_BASE  0x007FC000u

/* user stack top */
#define USER_STACK_TOP   0x00800000u

/**
 * check whether a pointer with a given length falls entirely within
 * the user address space. used to validate syscall arguments before
 * the kernel dereferences them.
 *
 * @param ptr pointer to validate.
 * @param len number of bytes the kernel will access starting at ptr.
 * @return true if the entire range [ptr, ptr+len) is in user space.
 */
static inline bool is_user_ptr(const void *ptr, uint32_t len)
{
    uint32_t addr = (uint32_t)ptr;

    /* null check */
    if (addr == 0)
        return false;

    /* must be within user space bounds */
    if (addr < USER_SPACE_START)
        return false;

    if (addr >= USER_SPACE_END)
        return false;

    /* overflow check: addr + len must not wrap or exceed user space */
    if (len > 0 && (addr + len < addr || addr + len > USER_SPACE_END))
        return false;

    return true;
}

/**
 * check whether a user string pointer is valid.
 * only checks that the pointer itself is in user space, not the full
 * string length (we can't know the length without reading it).
 * callers should bound string reads with a maximum length.
 *
 * @param str pointer to validate.
 * @return true if str points into user space.
 */
static inline bool is_user_str(const char *str)
{
    return is_user_ptr(str, 1);
}
