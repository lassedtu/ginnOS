#include "syscall.h"
#include "syscall_internal.h"
#include "fd_table.h"

#include "arch/x86/cpu/isr.h"
#include "arch/x86/cpu/idt.h"
#include "arch/x86/cpu/gdt.h"

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
        regs->eax = (uint32_t)(-1); /* -ENOSYS */
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
