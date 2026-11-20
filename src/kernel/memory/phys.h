#pragma once

/**
 * physical addresses and the conversion between physical and virtual.
 *
 * all RAM is identity-mapped today, so a physical address *is* a usable
 * pointer and these conversions are the identity. every place that turns a
 * physical address into a pointer (or back) goes through them anyway, so the
 * higher-half kernel (K3, a direct map at an offset) only changes these two
 * functions instead of every cast.
 */

#include "common/stdint.h"

// a physical address (pointer-width for now; PAE or x86_64 may widen it)
typedef uintptr_t phys_addr_t;

/**
 * pointer through which the kernel reaches a physical address.
 * @param phys the physical address.
 * @return the kernel virtual address of that byte.
 */
static inline void *phys_to_virt(phys_addr_t phys)
{
    return (void *)phys;
}

/**
 * physical address behind a kernel pointer into RAM (not the kernel stack
 * region or other remapped memory).
 * @param virt a kernel pointer into directly mapped RAM.
 * @return its physical address.
 */
static inline phys_addr_t virt_to_phys(const void *virt)
{
    return (phys_addr_t)virt;
}
