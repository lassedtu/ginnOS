#pragma once

/**
 * minimal COM1 serial port driver (16550 UART), polling-mode transmit.
 *
 * intended for kernel debug output: it works before the VGA console is up
 * and is visible headlessly under QEMU with `-serial stdio`. transmit only
 * blocks by polling the line-status register, so it needs no IRQ and is
 * safe to call from panic and early boot.
 */

#include "common/stdint.h"

/**
 * initialize COM1 (115200 baud, 8N1, FIFO enabled).
 * safe to call once, early in boot, before interrupts are enabled.
 */
void serial_initialize(void);

/**
 * write a single character to COM1, blocking until the UART is ready.
 * translates '\n' to "\r\n" so terminals render lines correctly.
 * @param c character to send.
 */
void serial_putchar(char c);

/**
 * write a null-terminated string to COM1.
 * @param str string to send.
 */
void serial_write(const char *str);
