#pragma once

/**
 * symbolised kernel backtraces for panics and exceptions.
 * output goes to the console and to the serial log.
 */

#include "common/stdint.h"

// deepest backtrace printed.
#define BACKTRACE_MAX_FRAMES 16

/**
 * print one code address as "0x0001234a func+0x1c".
 * @param label text printed before the address (e.g. "at" or "#3").
 * @param pc the code address.
 */
void backtrace_print_pc(const char *label, uintptr_t pc);

/**
 * print one return address. like backtrace_print_pc, but the symbol is
 * looked up for the call instruction before it (ret - 1).
 * @param label text printed before the address (e.g. "?").
 * @param ret the return address.
 */
void backtrace_print_return(const char *label, uintptr_t ret);

/**
 * print the return addresses found by walking frame pointers from frame.
 * stops at the first address outside kernel code (e.g. a user-mode frame).
 * @param frame the frame pointer to start from.
 */
void backtrace_print(uintptr_t frame);
