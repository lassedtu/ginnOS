#pragma once

#include "common/stdint.h"

#ifdef __x86_64__

/**
 * saved register state on the kernel stack (x86_64). the layout matches
 * isr_common in asm64/isr.asm:
 *   1. the general-purpose registers (pushed by isr_common)
 *   2. interrupt number and error code (pushed by the stub or the CPU)
 *   3. rip, cs, rflags, rsp, ss (pushed by the CPU on every interrupt)
 * the syscall entry (asm64/syscall.asm) makes the same frame.
 */
struct registers
{
    uintptr_t r15, r14, r13, r12, r11, r10, r9, r8;
    uintptr_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uintptr_t interrupt; // interrupt vector number.
    uintptr_t error;     // error code (or zero for interrupts without one).
    uintptr_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

#define REG_IP(r)     ((r)->rip)
#define REG_SP(r)     ((r)->rsp)
#define REG_FP(r)     ((r)->rbp)
#define REG_FLAGS(r)  ((r)->rflags)
#define REG_RESULT(r) ((r)->rax)

#else

/**
 * saved register state pushed onto the stack by ISR stubs.
 * this structure matches the layout created by the assembly ISR common handler:
 *   1. data segment (pushed by us)
 *   2. general-purpose registers (pushed by pusha)
 *   3. interrupt number and error code (pushed by stub / CPU)
 *   4. eip, cs, eflags, esp, ss (pushed automatically by the CPU)
 */
struct registers
{
    uint32_t ds;                                          // data segment selector.
    uint32_t edi, esi, ebp, kern_esp, ebx, edx, ecx, eax; // general-purpose registers from pusha.
    uint32_t interrupt;                                   // interrupt vector number.
    uint32_t error;                                       // error code (or zero for interrupts without one).
    uint32_t eip, cs, eflags, esp, ss;                    // pushed automatically by the CPU on interrupt.
} __attribute__((packed));

#define REG_IP(r)     ((r)->eip)
#define REG_SP(r)     ((r)->esp)
#define REG_FP(r)     ((r)->ebp)
#define REG_FLAGS(r)  ((r)->eflags)
#define REG_RESULT(r) ((r)->eax)

#endif

// the interrupt flag in EFLAGS (RFLAGS): set when interrupts are enabled
#define EFLAGS_IF 0x200u

/*
 * REG_IP, REG_SP, REG_FP, REG_FLAGS and REG_RESULT name the instruction
 * pointer, the stack pointer, the frame pointer, the flags and the
 * accumulator (the syscall number in, the result out) of a struct
 * registers. portable code uses them instead of the register names.
 */

/**
 * true if the interrupted code ran in user mode (ring 3).
 */
static inline bool regs_from_user(const struct registers *regs)
{
    return (regs->cs & 3u) == 3u;
}

/**
 * interrupt service routine handler function type.
 * @param regs pointer to the saved register state at the time of the interrupt.
 */
typedef void (*isr_handler_t)(struct registers *regs);

/**
 * initialize the interrupt service routine subsystem.
 * installs all 256 ISR gates into the IDT and enables them.
 */
void isr_initialize(void);

/**
 * register a handler function for a specific interrupt vector.
 * @param vector interrupt vector number (0–255).
 * @param handler pointer to the handler function to call when this interrupt fires.
 */
void isr_register_handler(int vector, isr_handler_t handler);
