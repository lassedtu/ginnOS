#include "syscall_internal.h"
#include "syscall.h"
#include "fd_table.h"

#include "kernel/vfs/file.h"
#include "kernel/vfs/poll.h"
#include "kernel/vfs/permission.h"
#include "kernel/time/timer.h"
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

    int32_t written = file_write(entry->file, buf, count);
    if (written == -EPIPE)
    {
        // nobody will read it: by default this ends the writer
        signal_send(process_current(), SIGPIPE);
    }
    return written;
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

// the flags an open-file description keeps (the rest only steer open)
#define OPEN_STATUS_FLAGS (O_ACCMODE | O_APPEND | O_NONBLOCK)

/**
 * create a file for O_CREAT, unless it exists (and O_EXCL is not set).
 * @return 0, or a negative errno.
 */
static int32_t open_create(const char *path, uint32_t flags)
{
    kerr_t err = vfs_create(path);
    if (err == KERR_EXIST)
    {
        return (flags & O_EXCL) ? -EEXIST : 0;
    }
    return kerr_to_errno(err);
}

/**
 * SYS_open: open a file by path.
 * args: EBX = path string pointer, ECX = flags (O_*), EDX = mode for a
 *       created file (taken through the umask; the caller's effective user
 *       and group own it).
 * returns the lowest free fd, or negative errno on failure:
 *   EEXIST  O_CREAT | O_EXCL and the file exists
 *   EACCES  the file's mode does not allow the access mode, or a new
 *           file's directory is not writable
 *   ENOTDIR O_DIRECTORY and it is not a directory
 *   EISDIR  a directory opened for writing
 *   EINVAL  an access mode other than O_RDONLY, O_WRONLY or O_RDWR
 */
static int32_t open_at(int32_t dirfd, const char *path, uint32_t flags, uint32_t mode)
{
    process_t *proc = process_current();

    uint32_t access = flags & O_ACCMODE;
    if (access == O_ACCMODE)
        return -EINVAL;

    // copy the path in from user memory, resolved against the cwd or dirfd
    char resolved[PATH_MAX];
    int32_t ret = resolve_user_path_at(dirfd, path, resolved, sizeof(resolved), true);
    if (ret < 0)
        return ret;

    // the rights the access mode needs, on an existing file; a new one
    // needs write and search rights on its directory
    vfs_stat_t existing;
    bool exists = kerr_ok(vfs_stat(resolved, &existing));
    if (exists)
    {
        if ((flags & O_CREAT) && (flags & O_EXCL))
            return -EEXIST;

        uint32_t want = 0;
        if (access != O_WRONLY)
            want |= MAY_READ;
        if (access != O_RDONLY || (flags & O_TRUNC))
            want |= MAY_WRITE;
        process_t *caller = process_current();
        ret = permission_check(&existing, caller->euid, caller->egid, want);
        if (ret < 0)
            return ret;
    }
    else if (flags & O_CREAT)
    {
        ret = permission_check_parent(resolved, false);
        if (ret == 0)
            ret = open_create(resolved, flags);
        if (ret == 0)
            ret = set_new_owner(resolved, mode);
        if (ret < 0)
            return ret;
    }

    file_t *file;
    kerr_t err = file_open(resolved, flags & OPEN_STATUS_FLAGS, &file);
    if (kerr_failed(err))
        return kerr_to_errno(err);

    uint8_t type = vfs_file_type(&file->vfs);
    if ((flags & O_DIRECTORY) && type != FS_TYPE_DIR)
        ret = -ENOTDIR;
    else if (type == FS_TYPE_DIR && access != O_RDONLY)
        ret = -EISDIR;
    else if ((flags & O_TRUNC) && access != O_RDONLY && type == FS_TYPE_FILE)
        ret = file_truncate(file, 0);

    if (ret < 0)
    {
        file_unref(file);
        return ret;
    }

    int fd = fd_install(proc, file, (flags & O_CLOEXEC) ? FD_CLOEXEC : 0, 0);
    if (fd < 0)
        file_unref(file);
    return fd;
}

syscall_ret_t sys_open(struct registers *regs)
{
    return open_at(AT_FDCWD, (const char *)syscall_arg(regs, 0), (uint32_t)syscall_arg(regs, 1),
                   (uint32_t)syscall_arg(regs, 2));
}

/**
 * SYS_openat: open relative to a directory.
 * args: dirfd (AT_FDCWD: the working directory), path, flags, mode.
 */
syscall_ret_t sys_openat(struct registers *regs)
{
    return open_at((int32_t)syscall_arg(regs, 0), (const char *)syscall_arg(regs, 1),
                   (uint32_t)syscall_arg(regs, 2), (uint32_t)syscall_arg(regs, 3));
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

// the largest file size (st_size is 32 bits)
#define FILE_SIZE_MAX 0x7FFFFFFFl

/**
 * SYS_ftruncate: cut or grow an open file to a length.
 * args: fd, length (the new bytes read as zeros). the offset stays.
 * returns 0, or a negative errno (EINVAL: a negative length, or a file
 * not open for writing; EFBIG: too long).
 */
syscall_ret_t sys_ftruncate(struct registers *regs)
{
    fd_entry_t *entry = fd_get((int)syscall_arg(regs, 0));
    if (!entry)
        return -EBADF;
    long length = (long)syscall_arg(regs, 1);
    if (length < 0)
        return -EINVAL;
    if (length > FILE_SIZE_MAX)
        return -EFBIG;

    return file_truncate(entry->file, (uint32_t)length);
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

/**
 * SYS_fstat: get the metadata of an open file.
 * args: EBX = fd, ECX = struct stat pointer.
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_fstat(struct registers *regs)
{
    struct kstat *user_stat = (struct kstat *)syscall_arg(regs, 1);
    fd_entry_t *entry = fd_get((int)syscall_arg(regs, 0));
    if (!entry)
        return -EBADF;

    struct kstat st;
    int32_t err = file_stat(entry->file, &st);
    if (err < 0)
        return err;
    return kerr_to_errno(copy_to_user(user_stat, &st, sizeof(st)));
}

// fcntl commands
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_DUPFD_CLOEXEC 1030

// the status flags F_SETFL may change
#define SETFL_FLAGS (O_APPEND | O_NONBLOCK)

/**
 * SYS_fcntl: control an open file descriptor.
 * args: EBX = fd, ECX = command, EDX = argument.
 *   F_DUPFD / F_DUPFD_CLOEXEC: duplicate to the lowest free fd >= argument
 *   F_GETFD / F_SETFD: the descriptor flags (FD_CLOEXEC)
 *   F_GETFL / F_SETFL: the description's status flags; only O_APPEND and
 *                      O_NONBLOCK can be changed
 * returns the command's result, or a negative errno.
 */
syscall_ret_t sys_fcntl(struct registers *regs)
{
    int fd = (int)syscall_arg(regs, 0);
    int command = (int)syscall_arg(regs, 1);
    uint32_t arg = (uint32_t)syscall_arg(regs, 2);
    process_t *proc = process_current();

    fd_entry_t *entry = fd_get(fd);
    if (!entry)
        return -EBADF;

    switch (command)
    {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
    {
        if ((int32_t)arg < 0 || (int32_t)arg >= FD_MAX)
            return -EINVAL;
        file_ref(entry->file);
        int new_fd = fd_install(proc, entry->file,
                                command == F_DUPFD_CLOEXEC ? FD_CLOEXEC : 0, (int)arg);
        if (new_fd < 0)
            file_unref(entry->file);
        return new_fd;
    }
    case F_GETFD:
        return (int32_t)entry->flags;
    case F_SETFD:
        entry->flags = arg & FD_CLOEXEC;
        return 0;
    case F_GETFL:
        return (int32_t)entry->file->flags;
    case F_SETFL:
        entry->file->flags = (entry->file->flags & ~(uint32_t)SETFL_FLAGS) | (arg & SETFL_FLAGS);
        return 0;
    default:
        return -EINVAL;
    }
}

/**
 * SYS_pread: read at an offset without moving the file offset.
 * args: EBX = fd, ECX = buffer, EDX = count, ESI = offset.
 * returns bytes read, or negative errno (ESPIPE for a pipe or the console).
 */
syscall_ret_t sys_pread(struct registers *regs)
{
    char *buf = (char *)syscall_arg(regs, 1);
    uint32_t count = (uint32_t)syscall_arg(regs, 2);

    if (!user_access_ok(buf, count, true))
        return -EFAULT;
    fd_entry_t *entry = fd_get((int)syscall_arg(regs, 0));
    if (!entry)
        return -EBADF;

    return file_pread(entry->file, buf, count, (int32_t)syscall_arg(regs, 3));
}

/**
 * SYS_pwrite: write at an offset without moving the file offset.
 * args: EBX = fd, ECX = buffer, EDX = count, ESI = offset.
 * returns bytes written, or negative errno (EINVAL with O_APPEND).
 */
syscall_ret_t sys_pwrite(struct registers *regs)
{
    const char *buf = (const char *)syscall_arg(regs, 1);
    uint32_t count = (uint32_t)syscall_arg(regs, 2);

    if (!user_access_ok(buf, count, false))
        return -EFAULT;
    fd_entry_t *entry = fd_get((int)syscall_arg(regs, 0));
    if (!entry)
        return -EBADF;

    return file_pwrite(entry->file, buf, count, (int32_t)syscall_arg(regs, 3));
}

/**
 * SYS_fsync: write an open file's data to disk.
 * args: EBX = fd.
 * ext2 writes through to the disk at once (there is no cache), so this
 * only checks the descriptor. returns 0 or -EBADF.
 */
syscall_ret_t sys_fsync(struct registers *regs)
{
    return fd_get((int)syscall_arg(regs, 0)) ? 0 : -EBADF;
}

// most descriptors one poll call may watch
#define POLL_FDS_MAX 64

/**
 * struct pollfd (ABI, as in libc poll.h).
 */
typedef struct
{
    int32_t fd;
    int16_t events;
    int16_t revents;
} kpollfd_t;

/**
 * fill in revents for each entry.
 * @return the number of entries with revents set.
 */
static int32_t poll_scan(kpollfd_t *fds, uint32_t count)
{
    int32_t ready = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        fds[i].revents = 0;
        if (fds[i].fd < 0)
        {
            continue; // ignored, as POSIX says
        }

        fd_entry_t *entry = fd_get(fds[i].fd);
        uint32_t events = POLLNVAL;
        if (entry)
        {
            // errors and hang-ups are reported even when not asked for
            uint32_t wanted = (uint16_t)fds[i].events | POLLERR | POLLHUP;
            events = file_poll(entry->file) & wanted;
        }

        fds[i].revents = (int16_t)events;
        if (events)
        {
            ready++;
        }
    }
    return ready;
}

/**
 * SYS_poll: wait until one of a set of descriptors is ready.
 * args: EBX = struct pollfd array, ECX = count (at most 64),
 *       EDX = timeout in milliseconds (negative: no limit, 0: do not wait).
 * returns the number of descriptors with events, 0 on timeout, or a
 * negative errno.
 */
syscall_ret_t sys_poll(struct registers *regs)
{
    kpollfd_t *user_fds = (kpollfd_t *)syscall_arg(regs, 0);
    uint32_t count = (uint32_t)syscall_arg(regs, 1);
    int32_t timeout = (int32_t)syscall_arg(regs, 2);

    if (count > POLL_FDS_MAX)
        return -EINVAL;

    kpollfd_t fds[POLL_FDS_MAX];
    if (kerr_failed(copy_from_user(fds, user_fds, count * sizeof(kpollfd_t))))
        return -EFAULT;

    uint64_t deadline = TIMER_NEVER;
    if (timeout >= 0)
        deadline = timer_now() + timer_ms_to_ticks((uint32_t)timeout);

    // masked from each scan until asleep, so a notify in between is not lost
    uint32_t flags = arch_irq_save();
    int32_t ready;
    for (;;)
    {
        ready = poll_scan(fds, count);
        if (ready > 0 || timeout == 0 || timer_now() >= deadline)
            break;
        if (signal_pending(process_current()))
        {
            // poll is never restarted: its timeout would start over
            arch_irq_restore(flags);
            return -EINTR;
        }
        poll_wait(deadline);
    }
    arch_irq_restore(flags);

    if (kerr_failed(copy_to_user(user_fds, fds, count * sizeof(kpollfd_t))))
        return -EFAULT;
    return ready;
}
