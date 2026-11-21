#pragma once

#include "common/boot/boot_info.h"
#include "kernel/memory/phys.h"

/**
 * stable kernel entry point invoked from early asm with boot metadata.
 * @param boot_phys physical address of the bootloader-provided boot_info_t.
 */
void cstart(phys_addr_t boot_phys);

/**
 * entry point for the c kernel after early asm setup.
 * @param boot pointer to bootloader-provided boot_info_t.
 * @return does not return.
 */
void kernel_main(boot_info_t *boot);