#pragma once

/**
 * @file sys/mman.h
 * @brief memory-mapping interface (userspace).
 *
 * the prot/flag values here are ABI and must match the kernel side in
 * src/kernel/memory/mman.h exactly.
 */

typedef unsigned int size_t;

// protection bits (prot argument). combine with bitwise OR.
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2

// mapping flags (flags argument).
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x04

// mmap failure return value.
#define MAP_FAILED ((void *)-1)

/**
 * map memory into the calling process's address space.
 *
 * two forms are supported:
 *   - anonymous (flags has MAP_ANONYMOUS, fd < 0): zero-filled pages.
 *   - device-backed (fd is an open device node such as "/dev/fb0"): maps the
 *     device's memory (e.g. the linear framebuffer).
 * the requested @p addr is advisory and currently ignored; the kernel picks a
 * free range in its mmap region.
 *
 * @param addr advisory address hint (may be NULL; ignored for now).
 * @param length number of bytes to map (rounded up to a page).
 * @param prot protection bits (PROT_*).
 * @param flags mapping flags (MAP_*).
 * @param fd device fd for a device-backed mapping, or -1 for anonymous.
 * @param offset byte offset into the mapping (must be page-aligned; 0 today).
 * @return the mapped address on success, or MAP_FAILED on error (errno set).
 */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
