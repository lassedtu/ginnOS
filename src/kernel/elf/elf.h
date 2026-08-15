#pragma once

#include "../../common/stdint.h"

// ELF32 file header magic numbers.
#define ELF_MAGIC_0 0x7F
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'

// ELF32 file header e_ident[] indexes.
#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define EI_NIDENT 16

// ELF32 file header class (32-bit).
#define ELFCLASS32 1

// ELF32 file header data encoding (little-endian).
#define ELFDATA2LSB 1

// ELF32 file header version (original).
#define EV_CURRENT 1

// ELF32 file header type (executable).
#define ET_EXEC 2

// ELF32 file header machine (Intel 80386).
#define EM_386 3

// ELF32 program header type (loadable segment).
#define PT_NULL 0
#define PT_LOAD 1

// ELF32 program header flags.
#define PF_X 0x1 /* executable */
#define PF_W 0x2 /* writable */
#define PF_R 0x4 /* readable */

/**
 * ELF32 file header.
 */
typedef struct
{
    uint8_t e_ident[EI_NIDENT];
    uint16_t e_type;      // object file type
    uint16_t e_machine;   // target architecture
    uint32_t e_version;   // original version of ELF
    uint32_t e_entry;     // entry point virtual address
    uint32_t e_phoff;     // program header table offset
    uint32_t e_shoff;     // section header table offset
    uint32_t e_flags;     // processor-specific flags
    uint16_t e_ehsize;    // ELF header size
    uint16_t e_phentsize; // program header entry size
    uint16_t e_phnum;     // number of program headers
    uint16_t e_shentsize; // section header entry size
    uint16_t e_shnum;     // number of section headers
    uint16_t e_shstrndx;  // section name string table index
} __attribute__((packed)) Elf32_Ehdr;

/**
 * ELF32 program header.
 */
typedef struct
{
    uint32_t p_type;   // PT_NULL, PT_LOAD, etc.
    uint32_t p_offset; // offset in file
    uint32_t p_vaddr;  // virtual address in memory
    uint32_t p_paddr;  // physical address (unused)
    uint32_t p_filesz; // size in file
    uint32_t p_memsz;  // size in memory (>= p_filesz, extra is zeroed)
    uint32_t p_flags;  // PF_X, PF_W, PF_R
    uint32_t p_align;  // alignment
} __attribute__((packed)) Elf32_Phdr;
