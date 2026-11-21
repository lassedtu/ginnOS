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
    long ret = _syscall(SYS_MMAP, (long)addr, (long)length, (prot & 0xFF) | (flags << 8), fd,
                       (long)offset);

    // the kernel returns either a mapped address or MAP_FAILED ((void*)-1).
    // a valid address can look negative, so test only against the
    // sentinel, not the sign.
    if ((void *)ret == MAP_FAILED)
    {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    return (void *)ret;
}

int munmap(void *addr, size_t length)
{
    int ret = _syscall(SYS_MUNMAP, (long)addr, (long)length, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return 0;
}

int mprotect(void *addr, size_t length, int prot)
{
    int ret = _syscall(SYS_MPROTECT, (long)addr, (long)length, prot, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return 0;
}

int shm_create(size_t size, int fd_flags)
{
    int ret = (int)_syscall(SYS_SHM_CREATE, (long)size, fd_flags, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}
