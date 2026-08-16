/**
 * @file line.h
 * @brief Line editor for shell input.
 */

#pragma once

#define LINE_MAX 256

/**
 * initialize the line editor.
 * switches terminal to raw mode.
 */
void line_init(void);

/**
 * set the prompt length (number of visible characters in the prompt).
 * must be called before line_read() so cursor positioning is correct.
 * @param len number of characters in the prompt.
 */
void line_set_prompt_len(int len);

/**
 * read a line of input with full editing support.
 * supports: cursor movement, insert/delete, home/end, ctrl shortcuts.
 * @param buf buffer to write the completed line into.
 * @param size size of the buffer.
 * @return number of characters in the line (excluding null terminator),
 *         or -1 on Ctrl+D with empty line (EOF).
 */
int line_read(char *buf, int size);
