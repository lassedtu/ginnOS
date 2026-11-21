#pragma once
#include "isr.h"

/**
 * initialize the exception handling subsystem.
 * this function registers a default exception handler for all CPU exceptions.
 */
void exception_initialize(void);

/**
 * default exception handler.
 * this function is called when a CPU exception occurs and no specific handler is registered for that exception
 * @param regs pointer to the registers structure containing the CPU state at the time of the exception.
 */
void exception_handler(struct registers *regs);

/**
 * print the general-purpose and segment registers of a trapped context.
 * @param regs the saved register state.
 */
void exception_dump_regs(const struct registers *regs);

/**
 * panic for a trapped context: prints the faulting pc and, for faults in
 * kernel mode, a backtrace from the trapped frame.
 * @param message description of the fault.
 * @param regs the saved register state.
 */
void exception_panic(const char *message, const struct registers *regs) __attribute__((noreturn));
