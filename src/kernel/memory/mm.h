#pragma once

/**
 * changing a process's user mappings: unmap and protect ranges, keeping the
 * process's areas (vma.h) and its page tables in step.
 */

#include "common/stdint.h"
#include "common/error.h"
#include "kernel/process/process.h"

/**
 * unmap [start, end) from a process: every mapped page is removed (frames
 * from RAM are freed; device frames are not) and the areas are trimmed.
 * unmapped parts of the range are not an error.
 * @param proc the process.
 * @param start first byte (page-aligned, user space).
 * @param end first byte past the range (page-aligned).
 * @return KERR_OK, KERR_INVAL for a bad range, KERR_NOSPC if splitting an
 *         area needs a slot and the table is full (nothing is unmapped then).
 */
kerr_t mm_unmap(process_t *proc, uintptr_t start, uintptr_t end);

/**
 * change the protection of [start, end) in a process. pages already backed
 * get the new permissions now; the rest are backed with them on first touch.
 * PROT_NONE keeps the pages but makes them inaccessible to user code.
 * @param proc the process.
 * @param start first byte (page-aligned, user space).
 * @param end first byte past the range (page-aligned).
 * @param prot the new PROT_* bits.
 * @return KERR_OK, KERR_INVAL for a bad range, KERR_NOENT if part of the
 *         range is not mapped, KERR_NOSPC if the areas cannot be split.
 */
kerr_t mm_protect(process_t *proc, uintptr_t start, uintptr_t end, uint32_t prot);
