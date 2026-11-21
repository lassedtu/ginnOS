#pragma once

#include "common/stdint.h"
#include "arch/arch.h"
#include "kernel/memory/vma.h"

// result of a failed ELF load.
#define ELF_LOAD_FAILED 0

/**
 * result of a successful ELF load.
 */
typedef struct
{
    uintptr_t entry; /* entry point virtual address */
    uintptr_t brk;   /* end of loaded segments (page-aligned, for sbrk) */
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
 * @param as the address space to map into.
 * @param vmas receives one area per loaded segment (overlapping segments
 *        fail the load).
 * @param result filled with entry point and program break on success.
 * @return true on success, false on failure (invalid ELF, I/O error, OOM).
 */
bool elf_load(const char *path, addr_space_t as, vma_table_t *vmas, elf_load_result_t *result);
