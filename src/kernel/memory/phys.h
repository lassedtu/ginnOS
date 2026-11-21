#pragma once

/**
 * physical addresses and the conversion between physical and virtual.
 *
 * the kernel runs in the higher half and reaches RAM through a direct map:
 * physical address P is at virtual P + KERNEL_VIRT_BASE (up to
 * PHYS_MAP_SIZE). device memory outside RAM is mapped with ioremap instead
 * (kernel/memory/mmio.h), and kernel stacks live in their own region, so
 * virt_to_phys only works on direct-mapped pointers (heap, PMM frames,
 * the kernel image).
 */

#include "common/stdint.h"
#include "kernel/memory/memory_layout.h"

// a physical address (pointer-width for now; PAE or x86_64 may widen it)
typedef uintptr_t phys_addr_t;

/**
 * pointer through which the kernel reaches a physical address.
 * @param phys the physical address.
 * @return the kernel virtual address of that byte.
 */
static inline void *phys_to_virt(phys_addr_t phys)
{
    return (void *)(phys + KERNEL_VIRT_BASE);
}

#ifdef __x86_64__

/* the physical address of the first byte of the kernel image, from limine
 * (arch/x86/boot64/limine_boot.c). the image is mapped at
 * KERNEL_IMAGE_BASE, not in the direct map. */
extern phys_addr_t kernel_image_phys;

/**
 * physical address behind a kernel pointer into RAM: the direct map or the
 * kernel image (not the kernel stack region or other remapped memory).
 * @param virt a kernel pointer into directly mapped RAM or the kernel image.
 * @return its physical address.
 */
static inline phys_addr_t virt_to_phys(const void *virt)
{
    uintptr_t addr = (uintptr_t)virt;
    if (addr >= KERNEL_IMAGE_BASE)
    {
        return addr - KERNEL_IMAGE_BASE + kernel_image_phys;
    }
    return addr - KERNEL_VIRT_BASE;
}

#else

/**
 * physical address behind a kernel pointer into RAM (not the kernel stack
 * region or other remapped memory).
 * @param virt a kernel pointer into directly mapped RAM.
 * @return its physical address.
 */
static inline phys_addr_t virt_to_phys(const void *virt)
{
    return (phys_addr_t)virt - KERNEL_VIRT_BASE;
}

#endif // __x86_64__
