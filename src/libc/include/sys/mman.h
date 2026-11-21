#pragma once

#include <sys/types.h>

/**
 * @file sys/mman.h
 * @brief memory-mapping interface (userspace).
 *
 * the prot/flag values here are ABI and must match the kernel side in
 * src/kernel/memory/mman.h exactly.
 */

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

// mapping flags (flags argument).
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x04
#define MAP_FIXED     0x10

// mmap failure return value.
#define MAP_FAILED ((void *)-1)

/**
 * map memory into the calling process's address space.
 *
 * two forms are supported:
 *   - anonymous (flags has MAP_ANONYMOUS, fd < 0): zero-filled pages.
 *   - device-backed (fd is an open device node such as "/dev/fb0"): maps the
 *     device's memory (e.g. the linear framebuffer).
 * with MAP_FIXED the mapping goes exactly at @p addr (page-aligned),
 * replacing anything mapped there; otherwise @p addr is a hint the kernel
 * uses if that range is free, else it picks a free range in its mmap region.
 *
 * @param addr address (MAP_FIXED) or hint (may be NULL).
 * @param length number of bytes to map (rounded up to a page).
 * @param prot protection bits (PROT_*).
 * @param flags mapping flags (MAP_*).
 * @param fd device fd for a device-backed mapping, or -1 for anonymous.
 * @param offset byte offset into the mapping (must be page-aligned; 0 today).
 * @return the mapped address on success, or MAP_FAILED on error (errno set).
 */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);

/**
 * remove the mappings in [addr, addr + length).
 * @param addr page-aligned start.
 * @param length number of bytes (rounded up to a page).
 * @return 0 on success, -1 on error (errno set).
 */
int munmap(void *addr, size_t length);

/**
 * change the protection of [addr, addr + length), which must be mapped.
 * @param addr page-aligned start.
 * @param length number of bytes (rounded up to a page).
 * @param prot the new PROT_* bits.
 * @return 0 on success, -1 on error (errno set; ENOMEM if part of the range
 *         is not mapped).
 */
int mprotect(void *addr, size_t length, int prot);
