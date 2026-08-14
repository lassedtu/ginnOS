#include "syscall.h"

#include "../../arch/x86/cpu/isr.h"
#include "../../arch/x86/cpu/idt.h"
#include "../../arch/x86/cpu/gdt.h"
#include "../../common/stdio.h"

/**
 * syscall function pointer type. takes a pointer to the CPU registers struct and returns an int32_t.
 * the registers struct contains the syscall number in EAX and arguments in EBX, ECX, EDX, ESI, EDI.
 * the return value is placed in EAX.
 */
typedef int32_t (*syscall_fn_t)(struct registers *regs);

static int32_t sys_exit(struct registers *regs);
static int32_t sys_write(struct registers *regs);

/**
 * syscall dispatch table. indexed by syscall number (EAX). unimplemented syscalls are NULL.
 */
static syscall_fn_t syscall_table[SYSCALL_COUNT] = {
    [SYS_EXIT] = sys_exit,
    [SYS_WRITE] = sys_write,
    [SYS_READ] = 0,
    [SYS_OPEN] = 0,
    [SYS_CLOSE] = 0,
    [SYS_STAT] = 0,
    [SYS_CREATE] = 0,
    [SYS_MKDIR] = 0,
    [SYS_EXEC] = 0,
    [SYS_GETPID] = 0,
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
