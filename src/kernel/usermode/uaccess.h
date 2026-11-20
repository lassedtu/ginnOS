#pragma once

/**
 * safe access to user memory from syscalls.
 *
 * a pointer from user space can be anything: unmapped, kernel memory, or a
 * buffer that runs off the end of a mapping. touching it blindly page-faults
 * in the kernel (a panic) or lets a program read or write kernel memory.
 * these helpers check the current process's page tables first, so a bad
 * pointer becomes KERR_FAULT (-EFAULT) instead.
 *
 * the check and the access are not atomic, which is fine for now: there is
 * one CPU and one thread per address space, and only the process itself can
 * change its mappings, never while it is inside a syscall.
 */

#include "common/stdint.h"
#include "common/error.h"

/**
 * check that the current process could access a whole user range.
 * @param ptr start of the range (a user address).
 * @param len length in bytes; 0 is always accessible.
 * @param write true if the kernel will write to the range.
 * @return true if every byte is mapped user memory with that access.
 */
bool user_access_ok(const void *ptr, size_t len, bool write);

/**
 * copy bytes from user memory into a kernel buffer.
 * @param dst kernel destination.
 * @param user_src user source.
 * @param len number of bytes.
 * @return KERR_OK, or KERR_FAULT (nothing copied) if the source is bad.
 */
kerr_t copy_from_user(void *dst, const void *user_src, size_t len);

/**
 * copy bytes from a kernel buffer into user memory.
 * @param user_dst user destination (must be writable).
 * @param src kernel source.
 * @param len number of bytes.
 * @return KERR_OK, or KERR_FAULT (nothing copied) if the destination is bad.
 */
kerr_t copy_to_user(void *user_dst, const void *src, size_t len);

/**
 * copy a NUL-terminated user string into a kernel buffer, checking each page
 * as the copy reaches it.
 * @param dst kernel destination.
 * @param user_src user string.
 * @param size size of dst, including room for the NUL.
 * @return the string length (without the NUL), KERR_FAULT if the string
 *         runs into memory the process cannot read, or KERR_RANGE if it
 *         does not fit in size bytes.
 */
int32_t strncpy_from_user(char *dst, const char *user_src, size_t size);
