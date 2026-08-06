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