#include "exception.h"
#include "../../../common/stdint.h"
#include "../../../common/stdio.h"
#include "../../../kernel/panic.h"

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

/**
 * default exception handler
 */
void exception_handler(struct registers *regs)
{
    printf(
        "\r\nEXCEPTION: %s (vector %u)\r\n",
        exception_names[regs->interrupt],
        regs->interrupt);

    printf(
        "error code: 0x%x\r\n",
        regs->error);

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
        regs->esp);

    printf(
        "eip=0x%x cs=0x%x eflags=0x%x ss=0x%x\r\n",
        regs->eip,
        regs->cs,
        regs->eflags,
        regs->ss);

    kernel_panic("CPU exception");
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