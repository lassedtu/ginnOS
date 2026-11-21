#pragma once

/**
 * virtual memory areas: what a process has mapped in its user half.
 *
 * each area is a page-aligned [start, end) range with protection bits and a
 * kind (ELF segment, heap, stack, anonymous or device mmap). a process keeps
 * its areas in a vma_table_t, sorted by start and never overlapping. the
 * table is what the page-fault handler consults and what mmap/munmap work
 * on.
 *
 * locking: only the owning process changes its table (in its own syscalls),
 * or exec before the process first runs, and there is one thread per
 * address space, so the table needs no lock.
 */

#include "common/stdint.h"
#include "common/error.h"

// most areas one process can have
#define VMA_MAX 32

/**
 * what backs an area.
 */
typedef enum
{
    VMA_ELF,    // a PT_LOAD segment of the program
    VMA_HEAP,   // the sbrk heap
    VMA_STACK,  // the user stack
    VMA_ANON,   // anonymous mmap (zero-filled)
    VMA_DEVICE, // device mmap (e.g. /dev/fb0)
    VMA_SHARED, // shared memory (shm_create), mapped up front, never copied on fork
} vma_kind_t;

/**
 * one area of user memory.
 */
typedef struct
{
    uintptr_t start; // first byte (page-aligned)
    uintptr_t end;   // first byte past the area (page-aligned)
    uint32_t prot;   // PROT_READ / PROT_WRITE / PROT_EXEC
    vma_kind_t kind; // what backs it
} vma_t;

/**
 * a process's areas, sorted by start.
 */
typedef struct
{
    vma_t areas[VMA_MAX];
    uint32_t count;
} vma_table_t;

/**
 * empty a table.
 * @param table the table.
 */
void vma_table_init(vma_table_t *table);

/**
 * add an area.
 * @param table the table.
 * @param start first byte (page-aligned).
 * @param end first byte past the area (page-aligned, > start).
 * @param prot PROT_* bits.
 * @param kind what backs it.
 * @return KERR_OK, KERR_INVAL for a bad range, KERR_EXIST if it overlaps an
 *         existing area, KERR_NOSPC if the table is full.
 */
kerr_t vma_insert(vma_table_t *table, uintptr_t start, uintptr_t end, uint32_t prot,
                  vma_kind_t kind);

/**
 * find the area containing an address.
 * @param table the table.
 * @param addr the address.
 * @return the area, or NULL if addr is not mapped by any.
 */
vma_t *vma_find(vma_table_t *table, uintptr_t addr);

/**
 * find the first area of a kind.
 * @param table the table.
 * @param kind the kind.
 * @return the area, or NULL if there is none.
 */
vma_t *vma_find_kind(vma_table_t *table, vma_kind_t kind);

/**
 * check that no area overlaps a range.
 * @param table the table.
 * @param start first byte of the range.
 * @param end first byte past the range.
 * @return true if [start, end) is entirely unmapped.
 */
bool vma_range_free(const vma_table_t *table, uintptr_t start, uintptr_t end);

/**
 * find the lowest free gap of a given size inside [low, high).
 * @param table the table.
 * @param length bytes needed (page-aligned).
 * @param low lowest acceptable start (page-aligned).
 * @param high first byte past the acceptable range.
 * @return the start of the gap, or 0 if none is large enough.
 */
uintptr_t vma_find_free(const vma_table_t *table, size_t length, uintptr_t low, uintptr_t high);

/**
 * unmap [start, end) from the table: areas inside it are removed, areas
 * crossing its edges are trimmed, and an area containing it is split in two.
 * @param table the table.
 * @param start first byte (page-aligned).
 * @param end first byte past the range (page-aligned).
 * @return KERR_OK (also when nothing was mapped there), KERR_INVAL for a bad
 *         range, KERR_NOSPC if a split needs a slot and the table is full.
 */
kerr_t vma_remove(vma_table_t *table, uintptr_t start, uintptr_t end);

/**
 * change the protection of [start, end). every byte of the range must be in
 * some area; areas crossing its edges are split so only the range changes.
 * @param table the table.
 * @param start first byte (page-aligned).
 * @param end first byte past the range (page-aligned).
 * @param prot the new PROT_* bits.
 * @return KERR_OK, KERR_INVAL for a bad range, KERR_NOENT if part of the
 *         range is unmapped, KERR_NOSPC if the splits need more slots than
 *         are free. on failure nothing changes.
 */
kerr_t vma_protect(vma_table_t *table, uintptr_t start, uintptr_t end, uint32_t prot);
