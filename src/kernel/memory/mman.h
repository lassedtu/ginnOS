#pragma once

/**
 * @file mman.h
 * @brief mmap protection and flag constants (kernel side).
 *
 * these values are ABI: they must match the libc mirror in
 * src/libc/include/sys/mman.h exactly, since userspace passes them across the
 * SYS_mmap boundary. kept small and POSIX-shaped.
 */

// protection bits (prot argument). combine with bitwise OR.
#define PROT_NONE  0x0 // pages may not be accessed
#define PROT_READ  0x1 // pages may be read
#define PROT_WRITE 0x2 // pages may be written

// mapping flags (flags argument).
#define MAP_SHARED    0x01 // changes are shared (device/shared memory)
#define MAP_PRIVATE   0x02 // changes are private to the process
#define MAP_ANONYMOUS 0x04 // not backed by a file; zero-filled (fd ignored)

// mmap failure return value (a userspace pointer sentinel).
#define MAP_FAILED ((void *)-1)
