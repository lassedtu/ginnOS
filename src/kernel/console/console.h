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

/**
 * read a line of input from the console.
 * this function will block until a line of input is available.
 * @param buffer pointer to a buffer to receive the input line.
 * @param size size of the buffer in bytes.
 * @return the number of characters read, or -1 if an error occurred.
 */
int console_readline(char *buffer, int size);