#include <sys/mman.h>

#include "errno.h"
#include "syscall.h"

/**
 * @file mman.c
 * @brief userspace mmap wrapper.
 */

void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset)
{
    // all five syscall registers are taken, so prot and flags share one
    int ret = _syscall(SYS_MMAP, (int)addr, (int)length, (prot & 0xFF) | (flags << 8), fd,
                       (int)offset);

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

int munmap(void *addr, size_t length)
{
    int ret = _syscall(SYS_MUNMAP, (int)addr, (int)length, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return 0;
}

int mprotect(void *addr, size_t length, int prot)
{
    int ret = _syscall(SYS_MPROTECT, (int)addr, (int)length, prot, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return 0;
}
