#pragma once

#include "common/boot/boot_info.h"
#include "kernel/memory/phys.h"

/**
 * stable kernel entry point invoked from early asm with boot metadata.
 * @param boot_phys physical address of the boot information: stage2's
 *        boot_info_t, or the multiboot 2 information.
 * @param magic MULTIBOOT2_BOOT_MAGIC for a multiboot 2 boot, else 0.
 */
void cstart(phys_addr_t boot_phys, uint32_t magic);

/**
 * entry point for the c kernel after early asm setup.
 * @param boot pointer to bootloader-provided boot_info_t.
 * @return does not return.
 */
void kernel_main(boot_info_t *boot);