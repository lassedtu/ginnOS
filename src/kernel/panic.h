#pragma once

#include "common/stdint.h"

/**
 * trigger a kernel panic, display error message and halt the system.
 * prints a backtrace of the caller.
 * @param message description of the panic condition.
 */
void kernel_panic(const char *message) __attribute__((noreturn));

/**
 * panic on behalf of a trapped context (cpu exception, page fault).
 * prints the faulting pc and a backtrace from the trapped frame instead of
 * the handler's own stack.
 * @param message description of the panic condition.
 * @param pc the faulting instruction address.
 * @param caller a probable return address into the faulting function's
 *               caller, printed as an uncertain "?" entry, or 0. this
 *               recovers the caller when the fault hit a function without a
 *               frame of its own (a frameless leaf or a prologue).
 * @param frame the frame pointer at the fault, or 0 to skip the backtrace
 *              (e.g. the fault came from user mode).
 */
void kernel_panic_at(const char *message, uint32_t pc, uint32_t caller, uint32_t frame)
    __attribute__((noreturn));
