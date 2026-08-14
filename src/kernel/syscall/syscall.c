#include "syscall.h"
#include "fd_table.h"

#include "../../arch/x86/cpu/isr.h"
#include "../../arch/x86/cpu/idt.h"
#include "../../arch/x86/cpu/gdt.h"
#include "../vfs/vfs.h"
#include "../console/console.h"
#include "../../drivers/keyboard/keyboard.h"
#include "../../common/stdio.h"

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
static int32_t sys_getpid(struct registers *regs);

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
    [SYS_EXEC] = 0,
    [SYS_GETPID] = sys_getpid,
    [SYS_WAITPID] = 0,
    [SYS_SBRK] = 0,
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
 * for now, halts the CPU (no process management yet).
 */
static int32_t sys_exit(struct registers *regs)
{
    int32_t code = (int32_t)regs->ebx;

    printf("sys_exit: process exited with code %d\r\n", code);

    /* no process to kill yet — just halt */
    for (;;)
    {
        __asm__ volatile("cli; hlt");
    }

    return 0;
}

/**
 * SYS_write: write bytes to a file descriptor.
 * args: EBX = fd, ECX = buffer pointer, EDX = count.
 * currently only supports fd 1 (stdout) via printf.
 * returns number of bytes written, or -1 on error.
 */
static int32_t sys_write(struct registers *regs)
{
    int32_t fd = (int32_t)regs->ebx;
    const char *buf = (const char *)regs->ecx;
    uint32_t count = regs->edx;

    /* only stdout (fd 1) and stderr (fd 2) for now */
    if (fd != 1 && fd != 2)
    {
        return -1;
    }

    /* basic validation: buffer must not be NULL */
    if (!buf)
    {
        return -1;
    }

    /* write each byte to the console */
    for (uint32_t i = 0; i < count; i++)
    {
        char c = buf[i];
        if (c == '\n')
        {
            printf("\r\n");
        }
        else
        {
            printf("%c", c);
        }
    }

    return (int32_t)count;
}

/**
 * SYS_open: open a file by path.
 * args: EBX = path string pointer, ECX = flags (unused for now).
 * returns fd number on success, -1 on failure.
 */
static int32_t sys_open(struct registers *regs)
{
    const char *path = (const char *)regs->ebx;
    (void)regs->ecx; /* flags — reserved for future use */

    if (!path)
    {
        return -1;
    }

    VFS_FILE file;

    if (!vfs_open(path, &file))
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

        /* line-buffered read: collect characters until newline or buffer full */
        uint32_t i = 0;
        while (i < count - 1)
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

        buf[i] = '\0';
        return (int32_t)i;
    }

    if (entry->type == FD_TYPE_FILE)
    {
        uint32_t bytes_read = vfs_read(&entry->file, count, buf);
        return (int32_t)bytes_read;
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

    VFS_STATUS status = vfs_stat(path, stat_out);
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

    if (!vfs_create(path))
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

    if (!vfs_mkdir(path))
    {
        return -1;
    }

    return 0;
}

/**
 * SYS_getpid: return the current process ID.
 * stub: always returns 1 until process management is implemented.
 */
static int32_t sys_getpid(struct registers *regs)
{
    (void)regs;
    return 1;
}
