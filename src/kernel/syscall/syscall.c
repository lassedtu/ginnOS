#include "syscall.h"
#include "fd_table.h"

#include "arch/x86/cpu/isr.h"
#include "arch/x86/cpu/idt.h"
#include "arch/x86/cpu/gdt.h"
#include "arch/x86/cpu/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/vfs/vfs.h"
#include "kernel/console/console.h"
#include "kernel/usermode/usermode.h"
#include "kernel/process/process.h"
#include "kernel/scheduler/scheduler.h"
#include "drivers/keyboard/keyboard.h"
#include "common/stdio.h"
#include "common/memory.h"
#include "common/string.h"

/**
 * syscall function pointer type. takes a pointer to the CPU registers struct and returns an int32_t.
 * the registers struct contains the syscall number in EAX and arguments in EBX, ECX, EDX, ESI, EDI.
 * the return value is placed in EAX.
 */
typedef int32_t (*syscall_fn_t)(struct registers *regs);

static int32_t sys_exit(struct registers *regs);
static int32_t sys_write(struct registers *regs);
static int32_t sys_read(struct registers *regs);
static int32_t sys_open(struct registers *regs);
static int32_t sys_close(struct registers *regs);
static int32_t sys_stat(struct registers *regs);
static int32_t sys_create(struct registers *regs);
static int32_t sys_mkdir(struct registers *regs);
static int32_t sys_exec(struct registers *regs);
static int32_t sys_getpid(struct registers *regs);
static int32_t sys_waitpid(struct registers *regs);
static int32_t sys_sbrk(struct registers *regs);
static int32_t sys_getcwd(struct registers *regs);
static int32_t sys_chdir(struct registers *regs);
static int32_t sys_readdir(struct registers *regs);
static int32_t sys_unlink(struct registers *regs);
static int32_t sys_rmdir(struct registers *regs);
static int32_t sys_ttyctl(struct registers *regs);
static int32_t sys_pipe(struct registers *regs);
static int32_t sys_dup2(struct registers *regs);
static int32_t sys_ftruncate(struct registers *regs);
static int32_t sys_lseek(struct registers *regs);

/**
 * syscall dispatch table. indexed by syscall number (EAX). unimplemented syscalls are NULL.
 */
static syscall_fn_t syscall_table[SYSCALL_COUNT] = {
    [SYS_EXIT] = sys_exit,
    [SYS_WRITE] = sys_write,
    [SYS_READ] = sys_read,
    [SYS_OPEN] = sys_open,
    [SYS_CLOSE] = sys_close,
    [SYS_STAT] = sys_stat,
    [SYS_CREATE] = sys_create,
    [SYS_MKDIR] = sys_mkdir,
    [SYS_EXEC] = sys_exec,
    [SYS_GETPID] = sys_getpid,
    [SYS_WAITPID] = sys_waitpid,
    [SYS_SBRK] = sys_sbrk,
    [SYS_GETCWD] = sys_getcwd,
    [SYS_CHDIR] = sys_chdir,
    [SYS_READDIR] = sys_readdir,
    [SYS_UNLINK] = sys_unlink,
    [SYS_RMDIR] = sys_rmdir,
    [SYS_TTYCTL] = sys_ttyctl,
    [SYS_PIPE] = sys_pipe,
    [SYS_DUP2] = sys_dup2,
    [SYS_FTRUNCATE] = sys_ftruncate,
    [SYS_LSEEK] = sys_lseek,
};

/**
 * int 0x80 handler. dispatches to the appropriate syscall based on EAX.
 * arguments are passed in EBX, ECX, EDX, ESI, EDI.
 * return value is placed in EAX.
 */
static void syscall_handler(struct registers *regs)
{
    uint32_t num = regs->eax;

    if (num >= SYSCALL_COUNT || !syscall_table[num])
    {
        regs->eax = (uint32_t)-1; /* ENOSYS */
        return;
    }

    regs->eax = (uint32_t)syscall_table[num](regs);
}

void syscall_initialize(void)
{
    fd_table_init();

    /* register the handler in the ISR dispatch table */
    isr_register_handler(SYSCALL_VECTOR, syscall_handler);

    /* overwrite the IDT gate to allow ring 3 invocation.
     * isr_init_gates set this vector as ring 0 interrupt gate;
     * we need it as a ring 3 trap gate (trap so IF stays set). */
    extern void isr_stub_128(void);
    idt_set_gate(
        SYSCALL_VECTOR,
        isr_stub_128,
        GDT_KERNEL_CODE,
        IDT_FLAG_RING3 | IDT_FLAG_GATE_32BIT_TRAP | IDT_FLAG_PRESENT);
}

/**
 * SYS_exit: terminate the current process.
 * arg: EBX = exit code.
 * returns control to the kernel (exec_program caller).
 */
static int32_t sys_exit(struct registers *regs)
{
    int32_t code = (int32_t)regs->ebx;

    usermode_exit(code);

    /* unreachable */
    return 0;
}

/**
 * SYS_write: write bytes to a file descriptor.
 * args: EBX = fd, ECX = buffer pointer, EDX = count.
 * supports console fds (1, 2), pipe fds, and file fds.
 * returns number of bytes written, or -1 on error.
 */
static int32_t sys_write(struct registers *regs)
{
    int32_t fd_num = (int32_t)regs->ebx;
    const char *buf = (const char *)regs->ecx;
    uint32_t count = regs->edx;

    if (!buf || count == 0)
    {
        return -1;
    }

    fd_entry_t *entry = fd_get(fd_num);
    if (!entry)
    {
        return -1;
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
            return -1;

        /* if read end is closed, broken pipe */
        if (pb->read_refs <= 0)
            return -1;

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

    return -1;
}

/**
 * SYS_open: open a file by path.
 * args: EBX = path string pointer, ECX = flags (unused for now).
 * returns fd number on success, -1 on failure.
 */
static int32_t sys_open(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;
    (void)regs->ecx; /* flags reserved for future use */

    if (!path)
    {
        return -1;
    }

    /* resolve relative paths against the process's cwd */
    char resolved[PATH_MAX];
    process_t *proc = process_current();
    const char *cwd = proc ? proc->cwd : "/";

    if (!vfs_resolve_path(cwd, path, resolved, sizeof(resolved)))
    {
        return -1;
    }

    VFS_FILE file;

    if (!vfs_open(resolved, &file))
    {
        return -1;
    }

    int fd = fd_alloc(&file);
    if (fd < 0)
    {
        vfs_close(&file);
        return -1;
    }

    return (int32_t)fd;
}

/**
 * SYS_close: close an open file descriptor.
 * args: EBX = fd.
 * returns 0 on success, -1 on failure.
 */
static int32_t sys_close(struct registers *regs)
{
    int fd = (int)regs->ebx;

    /* don't allow closing stdin/stdout/stderr */
    if (fd < 3)
    {
        return -1;
    }

    return (int32_t)fd_free(fd);
}

/**
 * SYS_read: read bytes from a file descriptor.
 * args: EBX = fd, ECX = buffer pointer, EDX = count.
 * for stdin (fd 0): line-buffered read from keyboard with echo.
 * for files: reads via VFS.
 * returns number of bytes read, or -1 on error.
 */
static int32_t sys_read(struct registers *regs)
{
    int fd_num = (int)regs->ebx;
    char *buf = (char *)regs->ecx;
    uint32_t count = regs->edx;

    if (!buf || count == 0)
    {
        return -1;
    }

    fd_entry_t *entry = fd_get(fd_num);
    if (!entry)
    {
        return -1;
    }

    if (entry->type == FD_TYPE_CONSOLE)
    {
        /* only stdin (fd 0) is readable */
        if (fd_num != 0)
        {
            return -1;
        }

        process_t *proc = process_current();

        if (proc && proc->tty_raw)
        {
            /* raw mode: deliver one keyboard_event_t per read call.
             * the buffer must be large enough to hold the struct (8 bytes). */
            if (count < sizeof(keyboard_event_t))
            {
                return -1;
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
            return -1;

        /* block until data is available or write end is closed */
        while (pb->count == 0)
        {
            if (pb->write_refs <= 0)
            {
                /* all write ends closed + buffer empty = EOF */
                return 0;
            }
            /* yield and try again (busy-wait with halt for now) */
            __asm__ volatile("hlt");
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

    return -1;
}

/**
 * SYS_stat: get file metadata.
 * args: EBX = path string pointer, ECX = pointer to stat output struct.
 * the output struct matches VFS_STAT (FS_STAT).
 * returns 0 on success, -1 on failure.
 */
static int32_t sys_stat(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;
    VFS_STAT *stat_out = (VFS_STAT *)regs->ecx;

    if (!path || !stat_out)
    {
        return -1;
    }

    /* resolve relative paths against the process's cwd */
    char resolved[PATH_MAX];
    process_t *proc = process_current();
    const char *cwd = proc ? proc->cwd : "/";

    if (!vfs_resolve_path(cwd, path, resolved, sizeof(resolved)))
    {
        return -1;
    }

    VFS_STATUS status = vfs_stat(resolved, stat_out);
    if (status != VFS_OK)
    {
        return -1;
    }

    return 0;
}

/**
 * SYS_create: create a regular file.
 * args: EBX = path string pointer.
 * returns 0 on success, -1 on failure.
 */
static int32_t sys_create(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;

    if (!path)
    {
        return -1;
    }

    /* resolve relative paths against the process's cwd */
    char resolved[PATH_MAX];
    process_t *proc = process_current();
    const char *cwd = proc ? proc->cwd : "/";

    if (!vfs_resolve_path(cwd, path, resolved, sizeof(resolved)))
    {
        return -1;
    }

    if (!vfs_create(resolved))
    {
        return -1;
    }

    return 0;
}

/**
 * SYS_mkdir: create a directory.
 * args: EBX = path string pointer.
 * returns 0 on success, -1 on failure.
 */
static int32_t sys_mkdir(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;

    if (!path)
    {
        return -1;
    }

    /* resolve relative paths against the process's cwd */
    char resolved[PATH_MAX];
    process_t *proc = process_current();
    const char *cwd = proc ? proc->cwd : "/";

    if (!vfs_resolve_path(cwd, path, resolved, sizeof(resolved)))
    {
        return -1;
    }

    if (!vfs_mkdir(resolved))
    {
        return -1;
    }

    return 0;
}

/**
 * SYS_getpid: return the current process ID.
 */
static int32_t sys_getpid(struct registers *regs)
{
    (void)regs;
    process_t *proc = process_current();
    if (!proc)
    {
        return -1;
    }
    return (int32_t)proc->pid;
}

/**
 * SYS_exec: load and execute an ELF binary.
 * args: EBX = path string pointer, ECX = argv array pointer (may be NULL).
 * does not return on success. returns -1 on failure.
 */
static int32_t sys_exec(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;
    const char **argv = (const char **)regs->ecx;

    if (!path)
    {
        return -1;
    }

    /* resolve relative paths against the process's cwd */
    char resolved[PATH_MAX];
    process_t *proc = process_current();
    const char *cwd = proc ? proc->cwd : "/";

    if (!vfs_resolve_path(cwd, path, resolved, sizeof(resolved)))
    {
        return -1;
    }

    return (int32_t)exec_program(resolved, argv);
}

/**
 * SYS_sbrk: grow the user program break.
 * args: EBX = increment (signed; only positive values supported for now).
 * returns the previous break address on success, or (uint32_t)-1 on failure.
 *
 * if increment is 0, simply returns the current break without changing it.
 * for positive increments, allocates physical pages and maps them as
 * user-accessible for any new pages between the old and new break.
 */
static int32_t sys_sbrk(struct registers *regs)
{
    int32_t increment = (int32_t)regs->ebx;
    uint32_t old_brk = usermode_get_brk();

    /* increment of 0: just return current break */
    if (increment == 0)
    {
        return (int32_t)old_brk;
    }

    /* negative increment not supported yet */
    if (increment < 0)
    {
        return (int32_t)(uint32_t)-1;
    }

    uint32_t new_brk = old_brk + (uint32_t)increment;

    /* overflow check */
    if (new_brk < old_brk)
    {
        return (int32_t)(uint32_t)-1;
    }

    /* allocate and map any new pages between old_brk and new_brk */
    uint32_t page = old_brk & ~(PAGE_SIZE - 1);
    uint32_t end = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    process_t *proc = process_current();
    uint32_t pd = proc ? proc->page_directory : paging_directory_address();

    for (; page < end; page += PAGE_SIZE)
    {
        void *frame = pmm_alloc_page();
        if (!frame)
        {
            return (int32_t)(uint32_t)-1;
        }

        memset(frame, 0, PAGE_SIZE);
        paging_map_in(pd, page, (uint32_t)frame, PTE_USER_RW);
    }

    usermode_set_brk(new_brk);
    return (int32_t)old_brk;
}

/**
 * SYS_waitpid: wait for a child process to exit.
 * args: EBX = child PID to wait for.
 * returns the child's exit code on success, or -1 on error.
 *
 * if the child is already a zombie, reaps it immediately.
 * otherwise, blocks the calling process until the child exits.
 */
static int32_t sys_waitpid(struct registers *regs)
{
    uint32_t child_pid = regs->ebx;
    process_t *parent = process_current();

    if (!parent)
    {
        return -1;
    }

    process_t *child = process_get(child_pid);
    if (!child)
    {
        return -1; /* no such process */
    }

    /* verify this is actually our child */
    if (child->parent_pid != parent->pid)
    {
        return -1; /* not our child */
    }

    /* if child is already a zombie, reap immediately */
    if (child->state == PROC_STATE_ZOMBIE)
    {
        int32_t code = child->exit_code;
        process_destroy(child);
        return code;
    }

    /* child is still running block the parent */
    parent->state = PROC_STATE_BLOCKED;
    parent->wait_for_pid = child_pid;
    scheduler_remove(parent);
    scheduler_yield();

    /* we've been woken up child should now be a zombie */
    child = process_get(child_pid);
    if (child && child->state == PROC_STATE_ZOMBIE)
    {
        int32_t code = child->exit_code;
        process_destroy(child);
        return code;
    }

    /* child disappeared somehow */
    return -1;
}

/**
 * SYS_getcwd: copy the current working directory into a user buffer.
 * args: EBX = buffer pointer, ECX = buffer size.
 * returns 0 on success, -1 on failure.
 */
static int32_t sys_getcwd(struct registers *regs)
{
    char *buf = (char *)regs->ebx;
    uint32_t size = regs->ecx;
    process_t *proc = process_current();

    if (!proc || !buf || size == 0)
    {
        return -1;
    }

    uint32_t len = strlen(proc->cwd);
    if (len + 1 > size)
    {
        return -1;
    }

    strcpy(buf, proc->cwd);
    return 0;
}

/**
 * SYS_chdir: change the current working directory.
 * args: EBX = path string pointer.
 * returns 0 on success, -1 on failure.
 */
static int32_t sys_chdir(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;
    process_t *proc = process_current();

    if (!proc || !path)
    {
        return -1;
    }

    // resolve the path relative to the current cwd
    char resolved[PATH_MAX];
    if (!vfs_resolve_path(proc->cwd, path, resolved, sizeof(resolved)))
    {
        return -1;
    }

    // verify the target is a valid directory
    VFS_STAT stat;
    if (vfs_stat(resolved, &stat) != VFS_OK)
    {
        return -1;
    }

    if (stat.file_type != FS_TYPE_DIR)
    {
        return -1;
    }

    strncpy(proc->cwd, resolved, PATH_MAX - 1);
    proc->cwd[PATH_MAX - 1] = '\0';
    return 0;
}

/**
 * SYS_readdir: read the next directory entry from an open directory fd.
 * args: EBX = fd, ECX = pointer to user dirent struct.
 *
 * user dirent layout (matches FS_DIRENT):
 *   uint32_t inode
 *   uint8_t  file_type
 *   uint32_t size
 *   char     name[256]
 *
 * returns 0 on success, -1 on failure or end of directory.
 */
static int32_t sys_readdir(struct registers *regs)
{
    int fd_num = (int)regs->ebx;
    FS_DIRENT *user_dirent = (FS_DIRENT *)regs->ecx;

    if (!user_dirent)
    {
        return -1;
    }

    fd_entry_t *entry = fd_get(fd_num);
    if (!entry || entry->type != FD_TYPE_FILE)
    {
        return -1;
    }

    FS_DIRENT dirent;
    if (!vfs_read_entry(&entry->file, &dirent))
    {
        return -1;
    }

    /* copy to userspace */
    memcpy(user_dirent, &dirent, sizeof(FS_DIRENT));
    return 0;
}

/**
 * SYS_unlink: remove a file.
 * args: EBX = path string pointer.
 * returns 0 on success, -1 on failure.
 */
static int32_t sys_unlink(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;

    if (!path)
    {
        return -1;
    }

    /* resolve relative paths against the process's cwd */
    char resolved[PATH_MAX];
    process_t *proc = process_current();
    const char *cwd = proc ? proc->cwd : "/";

    if (!vfs_resolve_path(cwd, path, resolved, sizeof(resolved)))
    {
        return -1;
    }

    if (!vfs_remove(resolved))
    {
        return -1;
    }

    return 0;
}

/**
 * SYS_rmdir: remove a directory.
 * args: EBX = path string pointer.
 * returns 0 on success, -1 on failure.
 */
static int32_t sys_rmdir(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;

    if (!path)
    {
        return -1;
    }

    /* resolve relative paths against the process's cwd */
    char resolved[PATH_MAX];
    process_t *proc = process_current();
    const char *cwd = proc ? proc->cwd : "/";

    if (!vfs_resolve_path(cwd, path, resolved, sizeof(resolved)))
    {
        return -1;
    }

    if (!vfs_rmdir(resolved))
    {
        return -1;
    }

    return 0;
}

/**
 * SYS_ttyctl: switch terminal mode for the calling process.
 * args: EBX = mode (0 = cooked/line-buffered, 1 = raw/event-based).
 * returns the previous mode on success, -1 on error.
 */
static int32_t sys_ttyctl(struct registers *regs)
{
    uint32_t mode = regs->ebx;
    process_t *proc = process_current();

    if (!proc)
        return -1;

    if (mode > 1)
        return -1;

    int32_t prev = (int32_t)proc->tty_raw;
    proc->tty_raw = (uint8_t)mode;
    return prev;
}

/**
 * SYS_pipe: create a pipe (pair of connected file descriptors).
 * args: EBX = pointer to int[2] array (receives [read_fd, write_fd]).
 * returns 0 on success, -1 on failure.
 */
static int32_t sys_pipe(struct registers *regs)
{
    int *user_fds = (int *)regs->ebx;
    process_t *proc = process_current();

    if (!proc || !user_fds)
        return -1;

    // allocate a pipe buffer
    pipe_buf_t *buf = pipe_alloc();
    if (!buf)
        return -1;

    // find two free fd slots
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
        return -1;
    }

    // set up read end
    proc->fds[read_fd].type = FD_TYPE_PIPE;
    proc->fds[read_fd].pipe.buf = buf;
    proc->fds[read_fd].pipe.dir = PIPE_READ;

    // set up write end
    proc->fds[write_fd].type = FD_TYPE_PIPE;
    proc->fds[write_fd].pipe.buf = buf;
    proc->fds[write_fd].pipe.dir = PIPE_WRITE;

    // return fd numbers to userspace
    user_fds[0] = read_fd;
    user_fds[1] = write_fd;

    return 0;
}

/**
 * SYS_dup2: duplicate a file descriptor to a specific number.
 * args: EBX = old_fd, ECX = new_fd.
 * if new_fd is already open, it is closed first.
 * returns new_fd on success, -1 on failure.
 */
static int32_t sys_dup2(struct registers *regs)
{
    int old_fd = (int)regs->ebx;
    int new_fd = (int)regs->ecx;
    process_t *proc = process_current();

    if (!proc)
        return -1;

    if (old_fd < 0 || old_fd >= FD_MAX || new_fd < 0 || new_fd >= FD_MAX)
        return -1;

    if (proc->fds[old_fd].type == FD_TYPE_NONE)
        return -1;

    // if same fd, just return it
    if (old_fd == new_fd)
        return new_fd;

    // close new_fd if it's already open
    if (proc->fds[new_fd].type != FD_TYPE_NONE)
    {
        fd_free(new_fd);
    }

    // copy the fd entry
    proc->fds[new_fd] = proc->fds[old_fd];

    // if it's a pipe, increment the ref counts
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
 * SYS_ftruncate: truncate an open file to zero length.
 * args: EBX = fd.
 * returns 0 on success, -1 on failure.
 */
static int32_t sys_ftruncate(struct registers *regs)
{
    int fd_num = (int)regs->ebx;
    fd_entry_t *entry = fd_get(fd_num);

    if (!entry || entry->type != FD_TYPE_FILE)
        return -1;

    if (!vfs_truncate(&entry->file))
        return -1;

    return 0;
}

/**
 * SYS_lseek: set the file cursor position.
 * args: EBX = fd, ECX = offset, EDX = whence (0=SET, 1=CUR, 2=END).
 * returns the new cursor position, or -1 on error.
 */
static int32_t sys_lseek(struct registers *regs)
{
    int fd_num = (int)regs->ebx;
    int32_t offset = (int32_t)regs->ecx;
    int whence = (int)regs->edx;

    fd_entry_t *entry = fd_get(fd_num);
    if (!entry || entry->type != FD_TYPE_FILE)
        return -1;

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
        return -1;
    }

    if (new_pos < 0)
        new_pos = 0;

    entry->file.file.ext2_file.cursor = (uint32_t)new_pos;
    return new_pos;
}
