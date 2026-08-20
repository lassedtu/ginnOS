#pragma once

#include "common/boot/boot_info.h"

/**
 * stable kernel entry point invoked from early asm with boot metadata.
 * @param boot pointer to bootloader-provided boot_info_t.
 */
void cstart(boot_info_t *boot);

/**
 * entry point for the c kernel after early asm setup.
 * @param boot pointer to bootloader-provided boot_info_t.
 * @return does not return.
 */
void kernel_main(boot_info_t *boot);