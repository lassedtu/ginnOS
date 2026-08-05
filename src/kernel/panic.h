#pragma once

/**
 * trigger a kernel panic, display error message and halt the system.
 * @param message description of the panic condition.
 */
void kernel_panic(const char *message) __attribute__((noreturn));
