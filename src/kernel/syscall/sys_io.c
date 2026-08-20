#include "syscall_internal.h"
#include "syscall.h"
#include "fd_table.h"

#include "arch/arch.h"
#include "kernel/vfs/vfs.h"
#include "kernel/console/console.h"
#include "kernel/process/process.h"
#include "drivers/keyboard/keyboard.h"
#include "common/memory.h"
#include "common/string.h"

/**
 * SYS_write: write bytes to a file descriptor.
 * args: EBX = fd, ECX = buffer pointer, EDX = count.
 * supports console fds (1, 2), pipe fds, and file fds.
 * returns number of bytes written, or negative errno on error.
 */
int32_t sys_write(struct registers *regs)
{
    int32_t fd_num = (int32_t)regs->ebx;
    const char *buf = (const char *)regs->ecx;
    uint32_t count = regs->edx;

    if (!is_user_ptr(buf, count))
        return -18; /* EFAULT */

    if (count == 0)
        return 0;

    fd_entry_t *entry = fd_get(fd_num);
    if (!entry)
    {
        return -2; /* EBADF */
    }

    if (entry->type == FD_TYPE_CONSOLE)
    {
        /* write each byte to the console */
        for (uint32_t i = 0; i < count; i++)
        {
            char c = buf[i];
            if (c == '\n')
            {
                console_putchar('\r');
                console_putchar('\n');
            }
            else
            {
                console_putchar(c);
            }
        }
        return (int32_t)count;
    }

    if (entry->type == FD_TYPE_PIPE)
    {
        pipe_buf_t *pb = entry->pipe.buf;

        /* only the write end can write */
        if (entry->pipe.dir != PIPE_WRITE)
            return -3; /* EINVAL */

        /* if read end is closed, broken pipe */
        if (pb->read_refs <= 0)
            return -7; /* EPIPE */

        /* write as many bytes as fit into the buffer */
        uint32_t written = 0;
        while (written < count)
        {
            if (pb->count >= PIPE_BUF_SIZE)
            {
                /* buffer full. for now, return short write */
                break;
            }

            pb->data[pb->write_pos] = buf[written];
            pb->write_pos = (pb->write_pos + 1) % PIPE_BUF_SIZE;
            pb->count++;
            written++;
        }

        return (int32_t)written;
    }

    if (entry->type == FD_TYPE_FILE)
    {
        uint32_t bytes_written = vfs_write(&entry->file, count, buf);
        return (int32_t)bytes_written;
    }

    return -2; /* EBADF */
}

/**
 * SYS_read: read bytes from a file descriptor.
 * args: EBX = fd, ECX = buffer pointer, EDX = count.
 * for stdin (fd 0): line-buffered read from keyboard with echo.
 * for files: reads via VFS.
 * returns number of bytes read, or negative errno on error.
 */
int32_t sys_read(struct registers *regs)
{
    int fd_num = (int)regs->ebx;
    char *buf = (char *)regs->ecx;
    uint32_t count = regs->edx;

    if (!is_user_ptr(buf, count))
        return -18; /* EFAULT */

    if (count == 0)
        return 0;

    fd_entry_t *entry = fd_get(fd_num);
    if (!entry)
    {
        return -2; /* EBADF */
    }

    if (entry->type == FD_TYPE_CONSOLE)
    {
        /* only stdin (fd 0) is readable */
        if (fd_num != 0)
        {
            return -2; /* EBADF */
        }

        process_t *proc = process_current();

        if (proc && proc->tty_raw)
        {
            /* raw mode: deliver one keyboard_event_t per read call.
             * the buffer must be large enough to hold the struct (8 bytes). */
            if (count < sizeof(keyboard_event_t))
            {
                return -3; /* EINVAL */
            }

            keyboard_event_t event;
            keyboard_wait_event(&event);

            memcpy(buf, &event, sizeof(keyboard_event_t));
            return (int32_t)sizeof(keyboard_event_t);
        }

        /* cooked mode: line-buffered read with echo */
        uint32_t i = 0;
        while (i < count)
        {
            char c = keyboard_read(); /* blocks until a key is available */

            if (c == '\n')
            {
                console_putchar('\r');
                console_putchar('\n');
                buf[i] = '\n';
                i++;
                break;
            }

            if (c == '\b')
            {
                if (i > 0)
                {
                    i--;
                    console_putchar('\b');
                }
                continue;
            }

            buf[i] = c;
            i++;
            console_putchar(c);
        }

        return (int32_t)i;
    }

    if (entry->type == FD_TYPE_FILE)
    {
        uint32_t bytes_read = vfs_read(&entry->file, count, buf);
        return (int32_t)bytes_read;
    }

    if (entry->type == FD_TYPE_PIPE)
    {
        pipe_buf_t *pb = entry->pipe.buf;

        /* only the read end can read */
        if (entry->pipe.dir != PIPE_READ)
            return -2; /* EBADF */

        /* block until data is available or write end is closed */
        while (pb->count == 0)
        {
            if (pb->write_refs <= 0)
            {
                /* all write ends closed + buffer empty = EOF */
                return 0;
            }
            /* yield and try again (busy-wait with halt for now) */
            arch_halt();
        }

        /* read as many bytes as available (up to count) */
        uint32_t to_read = pb->count < count ? pb->count : count;
        for (uint32_t i = 0; i < to_read; i++)
        {
            buf[i] = pb->data[pb->read_pos];
            pb->read_pos = (pb->read_pos + 1) % PIPE_BUF_SIZE;
        }
        pb->count -= to_read;

        return (int32_t)to_read;
    }

    return -2; /* EBADF */
}

/**
 * SYS_open: open a file by path.
 * args: EBX = path string pointer, ECX = flags (unused for now).
 * returns fd number on success, negative errno on failure.
 */
int32_t sys_open(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;
    (void)regs->ecx; /* flags reserved for future use */

    if (!is_user_str(path))
        return -18; /* EFAULT */

    /* resolve relative paths against the process's cwd */
    char resolved[PATH_MAX];
    process_t *proc = process_current();
    const char *cwd = proc ? proc->cwd : "/";

    if (!vfs_resolve_path(cwd, path, resolved, sizeof(resolved)))
    {
        return -3; /* EINVAL */
    }

    VFS_FILE file;
    kerr_t err = vfs_open(resolved, &file);
    if (kerr_failed(err))
    {
        return kerr_to_errno(err);
    }

    int fd = fd_alloc(&file);
    if (fd < 0)
    {
        vfs_close(&file);
        return -12; /* EMFILE */
    }

    return (int32_t)fd;
}

/**
 * SYS_close: close an open file descriptor.
 * args: EBX = fd.
 * returns 0 on success, negative errno on failure.
 */
int32_t sys_close(struct registers *regs)
{
    int fd = (int)regs->ebx;

    /* don't allow closing stdin/stdout/stderr */
    if (fd < 3)
    {
        return -3; /* EINVAL */
    }

    if (fd_free(fd) != 0)
    {
        return -2; /* EBADF */
    }

    return 0;
}

/**
 * SYS_lseek: set the file cursor position.
 * args: EBX = fd, ECX = offset, EDX = whence (0=SET, 1=CUR, 2=END).
 * returns the new cursor position, or negative errno on error.
 */
int32_t sys_lseek(struct registers *regs)
{
    int fd_num = (int)regs->ebx;
    int32_t offset = (int32_t)regs->ecx;
    int whence = (int)regs->edx;

    fd_entry_t *entry = fd_get(fd_num);
    if (!entry)
        return -2; /* EBADF */

    if (entry->type != FD_TYPE_FILE)
        return -8; /* ESPIPE */

    uint32_t size = entry->file.file.ext2_file.size;
    uint32_t cursor = entry->file.file.ext2_file.cursor;
    int32_t new_pos;

    switch (whence)
    {
    case 0: /* SEEK_SET */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR */
        new_pos = (int32_t)cursor + offset;
        break;
    case 2: /* SEEK_END */
        new_pos = (int32_t)size + offset;
        break;
    default:
        return -3; /* EINVAL */
    }

    if (new_pos < 0)
        new_pos = 0;

    entry->file.file.ext2_file.cursor = (uint32_t)new_pos;
    return new_pos;
}

/**
 * SYS_ftruncate: truncate an open file to zero length.
 * args: EBX = fd.
 * returns 0 on success, negative errno on failure.
 */
int32_t sys_ftruncate(struct registers *regs)
{
    int fd_num = (int)regs->ebx;
    fd_entry_t *entry = fd_get(fd_num);

    if (!entry)
        return -2; /* EBADF */

    if (entry->type != FD_TYPE_FILE)
        return -3; /* EINVAL */

    kerr_t err = vfs_truncate(&entry->file);
    if (kerr_failed(err))
        return kerr_to_errno(err);

    return 0;
}

/**
 * SYS_dup2: duplicate a file descriptor to a specific number.
 * args: EBX = old_fd, ECX = new_fd.
 * if new_fd is already open, it is closed first.
 * returns new_fd on success, negative errno on failure.
 */
int32_t sys_dup2(struct registers *regs)
{
    int old_fd = (int)regs->ebx;
    int new_fd = (int)regs->ecx;
    process_t *proc = process_current();

    if (!proc)
        return -3; /* EINVAL */

    if (old_fd < 0 || old_fd >= FD_MAX || new_fd < 0 || new_fd >= FD_MAX)
        return -2; /* EBADF */

    if (proc->fds[old_fd].type == FD_TYPE_NONE)
        return -2; /* EBADF */

    /* if same fd, just return it */
    if (old_fd == new_fd)
        return new_fd;

    /* close new_fd if it's already open */
    if (proc->fds[new_fd].type != FD_TYPE_NONE)
    {
        fd_free(new_fd);
    }

    /* copy the fd entry */
    proc->fds[new_fd] = proc->fds[old_fd];

    /* if it's a pipe, increment the ref counts */
    if (proc->fds[new_fd].type == FD_TYPE_PIPE)
    {
        proc->fds[new_fd].pipe.buf->ref_count++;
        if (proc->fds[new_fd].pipe.dir == PIPE_READ)
            proc->fds[new_fd].pipe.buf->read_refs++;
        else
            proc->fds[new_fd].pipe.buf->write_refs++;
    }

    return new_fd;
}

/**
 * SYS_pipe: create a pipe (pair of connected file descriptors).
 * args: EBX = pointer to int[2] array (receives [read_fd, write_fd]).
 * returns 0 on success, negative errno on failure.
 */
int32_t sys_pipe(struct registers *regs)
{
    int *user_fds = (int *)regs->ebx;
    process_t *proc = process_current();

    if (!is_user_ptr(user_fds, 2 * sizeof(int)))
        return -18; /* EFAULT */

    if (!proc)
        return -3; /* EINVAL */

    /* allocate a pipe buffer */
    pipe_buf_t *buf = pipe_alloc();
    if (!buf)
        return -21; /* ENOBUFS */

    /* find two free fd slots */
    int read_fd = -1;
    int write_fd = -1;

    for (int i = 3; i < FD_MAX && (read_fd < 0 || write_fd < 0); i++)
    {
        if (proc->fds[i].type == FD_TYPE_NONE)
        {
            if (read_fd < 0)
                read_fd = i;
            else
                write_fd = i;
        }
    }

    if (read_fd < 0 || write_fd < 0)
    {
        pipe_release(buf);
        return -12; /* EMFILE */
    }

    /* set up read end */
    proc->fds[read_fd].type = FD_TYPE_PIPE;
    proc->fds[read_fd].pipe.buf = buf;
    proc->fds[read_fd].pipe.dir = PIPE_READ;

    /* set up write end */
    proc->fds[write_fd].type = FD_TYPE_PIPE;
    proc->fds[write_fd].pipe.buf = buf;
    proc->fds[write_fd].pipe.dir = PIPE_WRITE;

    /* return fd numbers to userspace */
    user_fds[0] = read_fd;
    user_fds[1] = write_fd;

    return 0;
}
