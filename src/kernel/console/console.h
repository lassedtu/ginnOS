#pragma once

#include "common/boot/boot_info.h"

/**
 * initialize the console driver.
 * chooses the framebuffer backend when the bootloader provided a graphics-mode
 * framebuffer, otherwise the VGA text backend. must be called before any other
 * console functions are used.
 * @param boot boot information (carries the framebuffer description).
 */
void console_initialize(const boot_info_t *boot);

/**
 * write a single character to the console.
 * @param c the character to write
 */
void console_putchar(char c);

/**
 * write a null-terminated string to the console.
 * @param str the string to write
 */
void console_write(const char *str);

/**
 * write a buffer to the console as one update: the whole buffer is drawn,
 * then the caret is placed and the display flushed once. translates \n to
 * \r\n. this is the write syscall's path to the terminal.
 * @param buf bytes to write (need not be null-terminated).
 * @param len number of bytes.
 */
void console_write_bytes(const char *buf, uint32_t len);

/**
 * read a line of input from the console.
 * this function will block until a line of input is available.
 * @param buffer pointer to a buffer to receive the input line.
 * @param size size of the buffer in bytes.
 * @return the number of characters read, or -1 if an error occurred.
 */
int console_readline(char *buffer, int size);