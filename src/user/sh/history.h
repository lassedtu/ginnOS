// history.h - command history for the shell

#pragma once

#define HISTORY_MAX 128

/**
 * initialize the history subsystem.
 */
void history_init(void);

/**
 * add a command to history.
 * consecutive duplicates are not stored.
 * @param line the command string to store.
 */
void history_add(const char *line);

/**
 * get the number of entries in history.
 * @return number of stored entries.
 */
int history_count(void);

/**
 * get a history entry by index.
 * index 0 is the oldest entry, history_count()-1 is the newest.
 * @param index the index to retrieve.
 * @return pointer to the string, or NULL if out of range.
 */
const char *history_get(int index);

/**
 * navigate history upward (older).
 * call with the current line buffer to save the in-progress edit.
 * @param current the current line being edited (saved on first call).
 * @return pointer to the history entry to display, or NULL if at top.
 */
const char *history_prev(const char *current);

/**
 * navigate history downward (newer).
 * @return pointer to the history entry or the saved current line,
 *         or NULL if already at bottom.
 */
const char *history_next(void);

/**
 * reset the navigation cursor (called when a line is submitted).
 */
void history_reset_nav(void);
