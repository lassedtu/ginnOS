#pragma once

#include "common/stdint.h"

#ifdef __x86_64__

/**
 * ginnOS virtual memory layout constants (x86_64, 4-level paging, 48-bit
 * addresses). the user half is the lower canonical half; the kernel half is
 * the upper one.
 *   0x0000000000000000 - 0x0000000000000FFF : null guard (never mapped)
 *   0x0000000000400000 - ...                : user program text/data/bss
 *   ...                - ...                : user heap (sbrk)
 *   0x0000100000000000 - 0x00007EFFFFFFFFFF : user mmap region
 *   0x00007FFFFEFFF000 - 0x00007FFFFFFFEFFF : user stack (up to 16 MiB)
 *   0x00007FFFFFFFF000 - 0x00007FFFFFFFFFFF : user stack guard
 *   0xFFFF800000000000 - ...                : direct map of RAM (limine HHDM)
 *   0xFFFFC00000000000 - ...                : kernel stacks (kstack.h)
 *   0xFFFFD00000000000 - 0xFFFFD00003FFFFFF : kernel MMIO window (ioremap)
 *   0xFFFFFFFF80000000 - ...                : the kernel image
 */

#define USER_SPACE_START 0x0000000000001000ul
#define USER_SPACE_END   0x0000800000000000ul

/* the physical address of the kernel image is set by limine (phys.h) */

/* first physical byte above the legacy area (VGA memory, BIOS ROMs) */
#define LOW_MEMORY_END   0x00100000ul

#define USER_LOAD_ADDR   0x00400000ul

#define USER_STACK_TOP   0x00007FFFFFFFF000ul
#define USER_STACK_MAX   0x01000000ul
#define USER_STACK_LIMIT (USER_STACK_TOP - USER_STACK_MAX)
#define USER_STACK_SIZE  0x00004000ul
#define USER_STACK_BASE  (USER_STACK_TOP - USER_STACK_SIZE)

/* the direct map. limine maps the HHDM here (4-level paging, no
 * randomisation); the kernel checks it at boot and makes its own map of all
 * RAM at the same address */
#define KERNEL_VIRT_BASE 0xFFFF800000000000ul

/* most RAM the kernel uses (the direct map is 64 TiB, the PMM is smaller) */
#define PHYS_MAP_SIZE 0x0000001000000000ul

#define KSTACK_REGION_BASE 0xFFFFC00000000000ul

#define MMIO_REGION_BASE 0xFFFFD00000000000ul
#define MMIO_REGION_END  0xFFFFD00004000000ul

#define USER_MMAP_BASE 0x0000100000000000ul
#define USER_MMAP_END  0x00007F0000000000ul

/* the kernel image is linked here (linker/x86_64/kernel.ld) */
#define KERNEL_IMAGE_BASE 0xFFFFFFFF80000000ul

#else

/**
 * ginnOS virtual memory layout constants.
 *
 * every address space (i686, 4 GiB):
 *   0x00000000 - 0x00000FFF : null guard (never mapped)
 *   0x00400000 - ...        : user program text/data/bss (linker/user.ld)
 *   ...        - ...        : user heap (grows up via sbrk from the ELF end)
 *   0x40000000 - 0xAFFFFFFF : user mmap region
 *   0xBEFFF000 - 0xBFFFEFFF : user stack (up to 16 MiB, grows down on demand)
 *   0xBFFFF000 - 0xBFFFFFFF : user stack guard (never mapped)
 *   0xC0000000 - 0xDFFFFFFF : kernel: direct map of RAM (phys_to_virt)
 *   0xE0000000 - ...        : kernel stacks (kstack.h)
 *   0xF0000000 - 0xF3FFFFFF : kernel MMIO window (ioremap)
 *
 * the user half (below 0xC0000000) is per process; the kernel half is shared
 * by every address space and supervisor-only.
 */

/* lowest address a user pointer can validly reference (above the null guard) */
#define USER_SPACE_START 0x00001000u

/* one past the highest user-accessible address */
#define USER_SPACE_END   0xC0000000u

/* kernel physical load address */
#define KERNEL_PHYS_BASE 0x00010000u

/* first physical byte above the legacy area. the range from 0xA0000 to here
 * holds VGA memory and the BIOS ROMs, so boot-time allocations (the PMM
 * metadata and the first heap pages) start at this address or higher. */
#define LOW_MEMORY_END   0x00100000u

/* user program load address (matches linker/user.ld) */
#define USER_LOAD_ADDR   0x00400000u

/* top of the user stack (exclusive); the page above it is an unmapped guard */
#define USER_STACK_TOP   0xBFFFF000u

/* the user stack area: up to 16 MiB below USER_STACK_TOP, backed on demand
 * as the stack grows down */
#define USER_STACK_MAX   0x01000000u

/* lowest address the user stack may grow to */
#define USER_STACK_LIMIT (USER_STACK_TOP - USER_STACK_MAX)

/* the top of the stack, mapped before the process starts (argc/argv) */
#define USER_STACK_SIZE  0x00004000u

/* lowest address of the initially mapped part of the stack */
#define USER_STACK_BASE  (USER_STACK_TOP - USER_STACK_SIZE)

/* the kernel half: every address from here up is kernel-only and shared by
 * all address spaces. RAM is direct-mapped at its start (phys_to_virt). */
#define KERNEL_VIRT_BASE 0xC0000000u

/* most RAM the direct map covers on 32-bit (it must stay below the kernel
 * stack region); RAM above this is not used. x86_64 removes the cap. */
#define PHYS_MAP_SIZE 0x20000000u

/* kernel stacks: one slot per process, each an unmapped guard page followed
 * by the stack itself. kernel-only, shared by every address space (its page
 * table is reserved at boot), above user space. */
#define KSTACK_REGION_BASE 0xE0000000u

/* device memory (MMIO) mapped with ioremap, e.g. the framebuffer: a kernel
 * window shared by every address space. */
#define MMIO_REGION_BASE 0xF0000000u
#define MMIO_REGION_END  0xF4000000u

/* userspace mmap region: SYS_mmap hands out page-aligned virtual ranges here,
 * bumping upward per mapping. sits below kernel space (0xC0000000) and clear of
 * the program, heap, and stack. no unmap/reuse yet, just a bump cursor per
 * process. this is also where a device mapping (e.g. mmap of /dev/fb0) lands. */
#define USER_MMAP_BASE 0x40000000u
#define USER_MMAP_END  0xB0000000u

#endif // __x86_64__

/**
 * check whether a pointer with a given length falls entirely within
 * the user address space. used to validate syscall arguments before
 * the kernel dereferences them.
 *
 * @param ptr pointer to validate.
 * @param len number of bytes the kernel will access starting at ptr.
 * @return true if the entire range [ptr, ptr+len) is in user space.
 */
static inline bool is_user_ptr(const void *ptr, size_t len)
{
    uintptr_t addr = (uintptr_t)ptr;

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
