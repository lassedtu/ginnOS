#include "isr.h"
#include "idt.h"
#include "../../../common/stdint.h"
#include "../../../common/stdio.h"
#include "../../../kernel/panic.h"

/** handler dispatch table — one entry per interrupt vector. */
static isr_handler_t handlers[256];

/**
 * human-readable names for CPU exceptions 0–31.
 * entries left empty are reserved by Intel.
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

/** defined in isr_gen.c, installs all 256 ISR gates into the IDT. */
extern void isr_init_gates(void);

void isr_initialize(void)
{
    isr_init_gates();

    // enable all 256 IDT gates
    for (int i = 0; i < 256; i++)
    {
        idt_enable_gate(i);
    }
}

/**
 * common C-level interrupt handler called from the assembly ISR common stub.
 * dispatches to a registered handler if one exists, otherwise prints
 * a register dump and panics for unhandled CPU exceptions (vectors 0–31).
 * @param regs pointer to the saved register state on the stack.
 */
void __attribute__((cdecl)) isr_handler(struct registers *regs)
{
    if (handlers[regs->interrupt] != 0)
    {
        handlers[regs->interrupt](regs);
    }
    else if (regs->interrupt >= 32)
    {
        printf("Unhandled interrupt %u\r\n", regs->interrupt);
    }
    else
    {
        printf("EXCEPTION: %s (vector %u, error 0x%x)\r\n",
               exception_names[regs->interrupt],
               regs->interrupt,
               regs->error);

        printf("  eax=0x%x  ebx=0x%x  ecx=0x%x  edx=0x%x\r\n",
               regs->eax, regs->ebx, regs->ecx, regs->edx);

        printf("  esi=0x%x  edi=0x%x  ebp=0x%x  esp=0x%x\r\n",
               regs->esi, regs->edi, regs->ebp, regs->esp);

        printf("  eip=0x%x  cs=0x%x  eflags=0x%x  ss=0x%x\r\n",
               regs->eip, regs->cs, regs->eflags, regs->ss);

        kernel_panic("Unhandled CPU exception");
    }
}

void isr_register_handler(int vector, isr_handler_t handler)
{
    handlers[vector] = handler;
    idt_enable_gate(vector);
}
