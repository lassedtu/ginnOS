#include "exception.h"
#include "common/stdint.h"
#include "common/stdio.h"
#include "kernel/debug/ksyms.h"
#include "kernel/panic.h"
#include "kernel/klog/klog.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"

/**
 * exception names for the first 32 CPU exceptions.
 */
static const char *const exception_names[] =
    {
        "Divide by zero",                 //  0
        "Debug",                          //  1
        "Non-maskable interrupt",         //  2
        "Breakpoint",                     //  3
        "Overflow",                       //  4
        "Bound range exceeded",           //  5
        "Invalid opcode",                 //  6
        "Device not available",           //  7
        "Double fault",                   //  8
        "Coprocessor segment overrun",    //  9
        "Invalid TSS",                    // 10
        "Segment not present",            // 11
        "Stack-segment fault",            // 12
        "General protection fault",       // 13
        "Page fault",                     // 14
        "",                               // 15 (reserved)
        "x87 floating-point exception",   // 16
        "Alignment check",                // 17
        "Machine check",                  // 18
        "SIMD floating-point exception",  // 19
        "Virtualization exception",       // 20
        "Control protection exception",   // 21
        "",                               // 22 (reserved)
        "",                               // 23 (reserved)
        "",                               // 24 (reserved)
        "",                               // 25 (reserved)
        "",                               // 26 (reserved)
        "",                               // 27 (reserved)
        "Hypervisor injection exception", // 28
        "VMM communication exception",    // 29
        "Security exception",             // 30
        ""                                // 31 (reserved)
};

void exception_dump_regs(const struct registers *regs)
{
#ifdef __x86_64__
    // the CPU pushes ss:rsp on every interrupt in long mode
    printf("rax=0x%lx rbx=0x%lx rcx=0x%lx rdx=0x%lx\r\n", regs->rax, regs->rbx, regs->rcx,
           regs->rdx);
    printf("rsi=0x%lx rdi=0x%lx rbp=0x%lx rsp=0x%lx\r\n", regs->rsi, regs->rdi, regs->rbp,
           regs->rsp);
    printf("r8=0x%lx r9=0x%lx r10=0x%lx r11=0x%lx\r\n", regs->r8, regs->r9, regs->r10, regs->r11);
    printf("r12=0x%lx r13=0x%lx r14=0x%lx r15=0x%lx\r\n", regs->r12, regs->r13, regs->r14,
           regs->r15);
    printf("rip=0x%lx cs=0x%lx rflags=0x%lx ss=0x%lx\r\n", regs->rip, regs->cs, regs->rflags,
           regs->ss);
#else
    // esp/ss are only pushed on a privilege change. for a kernel-mode trap
    // the interrupted stack pointer is where the esp field would be.
    bool from_kernel = (regs->cs & 3u) == 0;
    uint32_t esp = from_kernel ? (uint32_t)(uintptr_t)&regs->esp : regs->esp;

    printf(
        "eax=0x%x ebx=0x%x ecx=0x%x edx=0x%x\r\n",
        regs->eax,
        regs->ebx,
        regs->ecx,
        regs->edx);

    printf(
        "esi=0x%x edi=0x%x ebp=0x%x esp=0x%x\r\n",
        regs->esi,
        regs->edi,
        regs->ebp,
        esp);

    if (from_kernel)
    {
        printf(
            "eip=0x%x cs=0x%x eflags=0x%x\r\n",
            regs->eip,
            regs->cs,
            regs->eflags);
    }
    else
    {
        printf(
            "eip=0x%x cs=0x%x eflags=0x%x ss=0x%x\r\n",
            regs->eip,
            regs->cs,
            regs->eflags,
            regs->ss);
    }
#endif
}

// true if the instruction ending just before ret is a near call: e8 rel32,
// or ff /2 (call through a register or memory operand).
static bool follows_call(uintptr_t ret)
{
    const uint8_t *code = (const uint8_t *)ret;

    if (code[-5] == 0xE8)
    {
        return true;
    }

    // ff /2: modrm reg field is 2. check the encodings by length: register
    // (2 bytes), [reg] (2), [reg+disp8] (3), [reg+disp32] or [disp32] (6).
    uint8_t modrm2 = code[-1];
    if (code[-2] == 0xFF && ((modrm2 >> 3) & 7u) == 2 &&
        ((modrm2 >> 6) == 3 || ((modrm2 >> 6) == 0 && (modrm2 & 7u) != 4 && (modrm2 & 7u) != 5)))
    {
        return true;
    }
    uint8_t modrm3 = code[-2];
    if (code[-3] == 0xFF && ((modrm3 >> 3) & 7u) == 2 && (modrm3 >> 6) == 1 && (modrm3 & 7u) != 4)
    {
        return true;
    }
    uint8_t modrm6 = code[-5];
    if (code[-6] == 0xFF && ((modrm6 >> 3) & 7u) == 2 && (modrm6 & 7u) != 4 &&
        ((modrm6 >> 6) == 2 || ((modrm6 >> 6) == 0 && (modrm6 & 7u) == 5)))
    {
        return true;
    }
    return false;
}

// a fault inside a function that has no frame of its own (a frameless leaf
// such as the src/common string routines, or a prologue) leaves its return
// address on top of the stack instead of in the ebp chain. return that word
// if it looks like a return address into kernel code, else 0. it is only a
// guess: a function with a frame may have a stale code address there.
static uintptr_t probable_caller(const struct registers *regs)
{
#ifdef __x86_64__
    // the CPU pushes the interrupted rsp: read the word on top of the stack
    uintptr_t top = *(const uintptr_t *)regs->rsp;
#else
    // a same-privilege trap does not push esp/ss, so the trap frame's esp
    // field overlays the interrupted stack: it holds the word on top of it.
    uintptr_t top = regs->esp;
#endif
    if (!ksym_is_kernel_text(top - 6) || !ksym_is_kernel_text(top - 1))
    {
        return 0;
    }
    return follows_call(top) ? top : 0;
}

void exception_panic(const char *message, const struct registers *regs)
{
    // only kernel frames can be symbolised; a user-mode fault has a user
    // ebp, so print just the faulting pc.
    bool from_kernel = (regs->cs & 3u) == 0;
    if (!from_kernel)
    {
        kernel_panic_at(message, REG_IP(regs), 0, 0);
    }
    kernel_panic_at(message, REG_IP(regs), probable_caller(regs), REG_FP(regs));
}

/**
 * default exception handler
 */
/**
 * the signal for a CPU exception in user code (0: not one a program can
 * cause; the kernel panics as before).
 */
static int signal_for_vector(uint32_t vector)
{
    switch (vector)
    {
    case 0:  // divide error
    case 16: // x87 floating point
    case 19: // SIMD floating point
        return SIGFPE;
    case 1: // debug
    case 3: // breakpoint (int3)
        return SIGTRAP;
    case 6: // invalid opcode
        return SIGILL;
    case 4:  // overflow (into)
    case 5:  // bound range
    case 11: // segment not present
    case 12: // stack segment
    case 13: // general protection
        return SIGSEGV;
    case 17: // alignment check
        return SIGBUS;
    default:
        return 0;
    }
}

void exception_handler(struct registers *regs)
{
    // a fault in user code is the program's: it gets a signal (by default
    // it dies) and the kernel goes on
    uint32_t vector = (uint32_t)regs->interrupt;
    int sig = signal_for_vector(vector);
    process_t *proc = process_current();
    if ((regs->cs & 3u) == 3u && sig != 0 && proc)
    {
        KLOG_WARN("pid %u: %s at pc 0x%lx, signal %d", proc->pid, exception_names[vector],
                  (unsigned long)REG_IP(regs), sig);
        proc->fault_addr = REG_IP(regs);
        proc->fault_write = false;
        signal_force(proc, sig);
        return;
    }

    printf(
        "\r\nEXCEPTION: %s (vector %u)\r\n",
        exception_names[vector],
        vector);

    printf(
        "error code: 0x%x\r\n",
        (uint32_t)regs->error);

    exception_dump_regs(regs);
    exception_panic("CPU exception", regs);
}

void exception_initialize(void)
{
    for (int i = 0; i < 32; i++)
    {
        isr_register_handler(
            i,
            exception_handler);
    }
}