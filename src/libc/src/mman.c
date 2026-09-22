#include <sys/mman.h>

#include "errno.h"
#include "syscall.h"

/**
 * @file mman.c
 * @brief userspace mmap wrapper.
 */

void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset)
{
    // the advisory addr is not passed to the kernel (it always picks the
    // range); the raw syscall takes length, prot, flags, fd, offset.
    (void)addr;

    int ret = _syscall(SYS_MMAP, (int)length, prot, flags, fd, (int)offset);

    // the kernel returns either a mapped address or MAP_FAILED ((void*)-1).
    // a valid address can be >= 0x80000000 (negative as int), so test only
    // against the sentinel, not sign.
    if ((void *)ret == MAP_FAILED)
    {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    return (void *)ret;
}
