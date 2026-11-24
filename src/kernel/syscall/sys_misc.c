#include "syscall_internal.h"
#include "syscall.h"

#include "kernel/random/random.h"
#include "kernel/vfs/vfs.h"
#include "kernel/process/process.h"
#include "common/string.h"
#include "fd_table.h"
#include "kernel/vfs/permission.h"
#include "kernel/acpi/acpi.h"
#include "kernel/klog/klog.h"

/**
 * SYS_getcwd: copy the current working directory into a user buffer.
 * args: EBX = buffer pointer, ECX = buffer size.
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_getcwd(struct registers *regs)
{
    char *buf = (char *)syscall_arg(regs, 0);
    size_t size = syscall_arg(regs, 1);
    process_t *proc = process_current();

    if (!proc || size == 0)
    {
        return -EINVAL;
    }

    // as the process sees it, inside its root directory
    const char *cwd = vfs_inside_root(proc->fs->cwd, proc->fs->root);
    size_t len = strlen(cwd);
    if (len + 1 > size)
    {
        return -ERANGE;
    }

    return kerr_to_errno(copy_to_user(buf, cwd, len + 1));
}

/**
 * SYS_chroot: make a directory the caller's root: its paths start there,
 * and ".." stops there (root only). the working directory stays; a tool
 * then changes it to "/".
 * args: path.
 * returns 0, or a negative errno (EPERM, ENOTDIR, ENOENT, EACCES).
 */
syscall_ret_t sys_chroot(struct registers *regs)
{
    process_t *proc = process_current();
    if (proc->euid != 0)
        return -EPERM;

    char resolved[PATH_MAX];
    int32_t err = resolve_user_path((const char *)syscall_arg(regs, 0), resolved,
                                    sizeof(resolved));
    if (err < 0)
        return err;
    vfs_stat_t st;
    kerr_t found = vfs_stat(resolved, &st);
    if (kerr_failed(found))
        return kerr_to_errno(found);
    if (st.file_type != FS_TYPE_DIR)
        return -ENOTDIR;

    strncpy(proc->fs->root, resolved, PATH_MAX - 1);
    proc->fs->root[PATH_MAX - 1] = '\0';
    return 0;
}

/**
 * SYS_chdir: change the current working directory.
 * args: EBX = path string pointer.
 * returns 0 on success, negative errno on failure.
 */
syscall_ret_t sys_chdir(struct registers *regs)
{
    const char *path = (const char *)syscall_arg(regs, 0);
    process_t *proc = process_current();

    if (!proc)
    {
        return -EINVAL;
    }

    /* copy the path in from user memory, resolved against the cwd */
    char resolved[PATH_MAX];
    int32_t path_err = resolve_user_path(path, resolved, sizeof(resolved));
    if (path_err < 0)
        return path_err;

    /* verify the target is a valid directory */
    vfs_stat_t stat;
    kerr_t err = vfs_stat(resolved, &stat);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }

    if (stat.file_type != FS_TYPE_DIR)
    {
        return -ENOTDIR;
    }

    // entering a directory needs its search (execute) right
    int32_t allowed = permission_check(&stat, proc->euid, proc->egid, MAY_EXEC);
    if (allowed < 0)
    {
        return allowed;
    }

    strncpy(proc->fs->cwd, resolved, PATH_MAX - 1);
    proc->fs->cwd[PATH_MAX - 1] = '\0';
    return 0;
}

/**
 * SYS_ioctl: issue a device-control request on an open file descriptor.
 * args: EBX = fd, ECX = request code, EDX = argument (usually a user pointer).
 * only file-backed fds carry a filesystem that can service ioctls; devfs
 * device nodes forward to the backing device. everything else is -ENOTTY.
 * returns 0 (or a request-specific value) on success, negative errno on error.
 *
 * note: the arg pointer is request-specific, so its size is only known to the
 * device handler; this layer passes it through and the handler validates it.
 */
syscall_ret_t sys_ioctl(struct registers *regs)
{
    int32_t fd_num = (int32_t)syscall_arg(regs, 0);
    uint32_t request = (uint32_t)syscall_arg(regs, 1);
    void *arg = (void *)syscall_arg(regs, 2);

    fd_entry_t *entry = fd_get(fd_num);
    if (!entry)
    {
        return -EBADF;
    }

    return file_ioctl(entry->file, request, arg);
}

#define RB_AUTOBOOT  0x01234567 // the values of <sys/reboot.h>, as on Linux
#define RB_POWER_OFF 0x4321FEDC

/**
 * SYS_reboot: restart (RB_AUTOBOOT) or turn off (RB_POWER_OFF) the
 * machine. only root (effective user 0) may. the disk writes are already
 * done: the block cache writes through.
 * returns -EPERM for other users and -EINVAL for another value; it does
 * not return for root.
 */
syscall_ret_t sys_reboot(struct registers *regs)
{
    uint32_t how = (uint32_t)syscall_arg(regs, 0);
    if (how != RB_AUTOBOOT && how != RB_POWER_OFF)
    {
        return -EINVAL;
    }
    if (process_current()->euid != 0)
    {
        return -EPERM;
    }
    if (how == RB_POWER_OFF)
    {
        KLOG_INFO("kernel: power off");
        acpi_power_off();
    }
    KLOG_INFO("kernel: reboot");
    acpi_reboot();
}

/**
 * SYS_getrandom: random bytes.
 * args: buf, length, flags (GRND_NONBLOCK, GRND_RANDOM, GRND_INSECURE).
 * waits until the generator is seeded, unless GRND_NONBLOCK or
 * GRND_INSECURE.
 * returns the bytes given, or -EAGAIN, -EINVAL, -EFAULT, -EINTR.
 */
syscall_ret_t sys_getrandom(struct registers *regs)
{
    return random_getrandom((void *)syscall_arg(regs, 0), (uint32_t)syscall_arg(regs, 1),
                            (uint32_t)syscall_arg(regs, 2));
}
