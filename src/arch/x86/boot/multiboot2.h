#pragma once

/**
 * @file multiboot2.h
 * @brief the multiboot 2 boot information, read into a boot_info_t.
 *
 * a multiboot 2 bootloader (limine, grub) gives the kernel a list of tags.
 * the kernel reads the tags it uses into its boot_info_t before it uses any
 * memory, because the bootloader puts the tags in memory that the kernel
 * later gives to the PMM.
 */

#include "common/boot/boot_info.h"
#include "common/stdint.h"

/* EAX at the kernel entry when a multiboot 2 bootloader starts the kernel */
#define MULTIBOOT2_BOOT_MAGIC 0x36D76289u

/**
 * read the multiboot 2 information into a boot_info_t. fields without a tag
 * stay zero. the command line is copied into kernel memory.
 * @param info_phys the physical address the bootloader gave in EBX.
 * @param boot the boot_info_t to fill in (zeroed by the caller).
 * @return false if the information is not in the direct map or is not
 *         valid.
 */
bool multiboot2_read(uintptr_t info_phys, boot_info_t *boot);
