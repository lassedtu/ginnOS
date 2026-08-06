#pragma once

/**
 * initialize the console driver.
 * this function must be called before any other console functions are used.
 */
void console_initialize(void);

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