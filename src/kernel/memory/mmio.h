#pragma once

/**
 * mapping device memory (MMIO) into the kernel.
 *
 * device memory such as a linear framebuffer sits outside RAM, so the direct
 * map of RAM does not cover it. ioremap maps it into a kernel-only window
 * (MMIO_REGION_BASE..MMIO_REGION_END) shared by every address space. mappings
 * are permanent: devices are never unplugged.
 */

#include "common/stdint.h"
#include "kernel/memory/phys.h"

/**
 * reserve the MMIO window. must run after paging_init() and before the first
 * address space is created.
 */
void mmio_init(void);

/**
 * map a range of device memory into the kernel.
 * @param phys physical address of the device memory (need not be aligned).
 * @param size number of bytes.
 * @return kernel pointer to phys, or NULL if the window is exhausted.
 */
void *ioremap(phys_addr_t phys, size_t size);
