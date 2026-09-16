#pragma once

#include "common/stdint.h"

// result of a failed ELF load.
#define ELF_LOAD_FAILED 0

/**
 * result of a successful ELF load.
 */
typedef struct
{
    uint32_t entry; /* entry point virtual address */
    uint32_t brk;   /* end of loaded segments (page-aligned, for sbrk) */
} elf_load_result_t;

/**
 * load an ELF executable from the filesystem into user memory.
 *
 * opens the file at path, validates the ELF header, iterates PT_LOAD
 * program headers, allocates physical pages, maps them as user-accessible
 * into the specified page directory, and copies segment data.
 * BSS regions (p_memsz > p_filesz) are zeroed.
 *
 * @param path absolute path to the ELF file on the filesystem.
 * @param pd_phys physical address of the page directory to map into.
 * @param result filled with entry point and program break on success.
 * @return true on success, false on failure (invalid ELF, I/O error, OOM).
 */
bool elf_load(const char *path, uint32_t pd_phys, elf_load_result_t *result);
