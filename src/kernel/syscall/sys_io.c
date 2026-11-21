#include "syscall_internal.h"
#include "syscall.h"
#include "fd_table.h"

#include "kernel/vfs/file.h"
#include "kernel/process/process.h"

/**
 * SYS_write: write bytes to a file descriptor.
 * args: EBX = fd, ECX = buffer pointer, EDX = count.
 * returns number of bytes written, or negative errno on error.
 */
syscall_ret_t sys_write(struct registers *regs)
{
    int32_t fd = (int32_t)syscall_arg(regs, 0);
    const char *buf = (const char *)syscall_arg(regs, 1);
    uint32_t count = (uint32_t)syscall_arg(regs, 2);

    // the whole buffer must be readable user memory
    if (!user_access_ok(buf, count, false))
        return -EFAULT;

    fd_entry_t *entry = fd_get(fd);
    if (!entry)
        return -EBADF;

    if (count == 0)
        return 0;

    return file_write(entry->file, buf, count);
}

/**
 * SYS_read: read bytes from a file descriptor.
 * args: EBX = fd, ECX = buffer pointer, EDX = count.
 * returns number of bytes read (0 at end of file), or negative errno.
 */
syscall_ret_t sys_read(struct registers *regs)
{
    int32_t fd = (int32_t)syscall_arg(regs, 0);
    char *buf = (char *)syscall_arg(regs, 1);
    uint32_t count = (uint32_t)syscall_arg(regs, 2);

    // the whole buffer must be writable user memory
    if (!user_access_ok(buf, count, true))
        return -EFAULT;

    fd_entry_t *entry = fd_get(fd);
    if (!entry)
        return -EBADF;

    if (count == 0)
        return 0;

    return file_read(entry->file, buf, count);
}

/**
 * SYS_open: open a file by path.
 * args: EBX = path string pointer, ECX = flags (not used yet: every file is
 *       opened for reading and writing).
 * returns the lowest free fd, or negative errno on failure.
 */
syscall_ret_t sys_open(struct registers *regs)
{
    const char *path = (const char *)syscall_arg(regs, 0);
    process_t *proc = process_current();

    // copy the path in from user memory, resolved against the cwd
    char resolved[PATH_MAX];
    int32_t path_err = resolve_user_path(path, resolved, sizeof(resolved));
    if (path_err < 0)
        return path_err;

    file_t *file;
    kerr_t err = file_open(resolved, O_RDWR, &file);
    if (kerr_failed(err))
        return kerr_to_errno(err);

    int fd = fd_install(proc, file, 0, 0);
    if (fd < 0)
        file_unref(file);
    return fd;
}

/**
 * SYS_close: close an open file descriptor.
 * args: EBX = fd.
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_close(struct registers *regs)
{
    return fd_close((int)syscall_arg(regs, 0));
}

/**
 * SYS_lseek: set the file offset.
 * args: EBX = fd, ECX = offset, EDX = whence (0=SET, 1=CUR, 2=END).
 * returns the new offset, or negative errno on error (ESPIPE for a pipe
 * or the console).
 */
syscall_ret_t sys_lseek(struct registers *regs)
{
    fd_entry_t *entry = fd_get((int)syscall_arg(regs, 0));
    if (!entry)
        return -EBADF;

    return file_seek(entry->file, (int32_t)syscall_arg(regs, 1), (int)syscall_arg(regs, 2));
}

/**
 * SYS_ftruncate: truncate an open file to zero length.
 * args: EBX = fd.
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_ftruncate(struct registers *regs)
{
    fd_entry_t *entry = fd_get((int)syscall_arg(regs, 0));
    if (!entry)
        return -EBADF;

    return file_truncate(entry->file);
}

/**
 * SYS_dup2: make new_fd another descriptor for old_fd's open file.
 * args: EBX = old_fd, ECX = new_fd.
 * if new_fd is already open, it is closed first. the new descriptor does
 * not have FD_CLOEXEC.
 * returns new_fd on success, negative errno on failure.
 */
syscall_ret_t sys_dup2(struct registers *regs)
{
    int old_fd = (int)syscall_arg(regs, 0);
    int new_fd = (int)syscall_arg(regs, 1);
    process_t *proc = process_current();

    fd_entry_t *old_entry = fd_get(old_fd);
    if (!old_entry || new_fd < 0 || new_fd >= FD_MAX)
        return -EBADF;

    if (old_fd == new_fd)
        return new_fd;

    // take the reference first: closing new_fd may drop the last other one
    file_t *file = old_entry->file;
    file_ref(file);
    if (proc->fds[new_fd].file)
        fd_close_entry(&proc->fds[new_fd]);
    proc->fds[new_fd].file = file;
    proc->fds[new_fd].flags = 0;

    return new_fd;
}

/**
 * SYS_pipe: create a pipe (pair of connected file descriptors).
 * args: EBX = pointer to int[2] array (receives [read_fd, write_fd]).
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_pipe(struct registers *regs)
{
    int *user_fds = (int *)syscall_arg(regs, 0);
    process_t *proc = process_current();

    // checked up front: the fds are installed before they are copied out
    if (!user_access_ok(user_fds, 2 * sizeof(int), true))
        return -EFAULT;

    file_t *read_end;
    file_t *write_end;
    if (kerr_failed(file_open_pipe(&read_end, &write_end)))
        return -ENOBUFS;

    int fds[2];
    fds[0] = fd_install(proc, read_end, 0, 0);
    fds[1] = fds[0] < 0 ? -EMFILE : fd_install(proc, write_end, 0, 0);
    if (fds[1] < 0)
    {
        if (fds[0] >= 0)
            proc->fds[fds[0]].file = NULL;
        file_unref(read_end);
        file_unref(write_end);
        return -EMFILE;
    }

    return kerr_to_errno(copy_to_user(user_fds, fds, sizeof(fds)));
}
