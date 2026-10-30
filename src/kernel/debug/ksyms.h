#pragma once

/**
 * kernel symbol table: maps code addresses to function names so panics and
 * exceptions can print a readable backtrace.
 *
 * the table is generated at build time from `nm -n build/kernel.elf` by
 * tools/gen_ksyms.py and linked into the .ksyms section (see the two-pass
 * kernel link in the Makefile).
 */

#include "common/stdint.h"

/**
 * one symbol: the address it starts at and where its name begins inside
 * ksym_names.
 */
typedef struct
{
    uint32_t addr;
    uint32_t name_offset;
} ksym_t;

// generated tables (sorted by address).
extern const uint32_t ksym_count;
extern const ksym_t ksym_table[];
extern const char ksym_names[];

/**
 * find the function containing an address.
 * @param addr a code address.
 * @param offset receives addr minus the function start; may be NULL.
 * @return the function name, or NULL if addr is not inside kernel code.
 */
const char *ksym_lookup(uint32_t addr, uint32_t *offset);

/**
 * check whether an address lies inside the kernel's .text section.
 * @param addr the address to check.
 * @return true if addr is kernel code.
 */
bool ksym_is_kernel_text(uint32_t addr);
