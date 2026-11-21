#pragma once

/**
 * @file ze_terminal_compat.h
 * @brief force-included into zen-editor's platforms/ginnos/terminal.c only.
 *
 * terminal.c declares size_t and ssize_t itself, as unsigned int and int,
 * to declare write and read without unistd.h. on x86_64 the libc types are
 * 64 bits, so those typedefs conflict. this header includes the libc
 * headers that terminal.c uses first, then renames the two types for the
 * rest of the file, so its typedefs make new names.
 *
 * its write and read declarations then have 32-bit counts and results.
 * that is safe with the SysV calling convention: the counts are small and
 * positive, and the caller reads only the low 32 bits of the result.
 * remove this header when zen-editor includes unistd.h for write and read.
 */

#include <stdio.h>
#include <string.h>

#define size_t  ze_terminal_size_t
#define ssize_t ze_terminal_ssize_t
