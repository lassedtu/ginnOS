// history.c - command history ring buffer

#include "history.h"
#include "line.h"

#include <string.h>

// ring buffer of history entries
static char history_buf[HISTORY_MAX][LINE_MAX];
static int history_size;  // number of valid entries (up to HISTORY_MAX)
static int history_start; // index of the oldest entry in the ring
static int history_end;   // index of the next write slot

// navigation state
static int nav_pos;              // current position during Up/Down navigation
                                 // -1 = not navigating (at the bottom/current line)
static char nav_saved[LINE_MAX]; // saved in-progress line before navigating

void history_init(void)
{
    history_size = 0;
    history_start = 0;
    history_end = 0;
    nav_pos = -1;
    nav_saved[0] = '\0';
}

void history_add(const char *line)
{
    // don't store empty lines
    if (!line || line[0] == '\0')
        return;

    // don't store consecutive duplicates
    if (history_size > 0)
    {
        int last = (history_end - 1 + HISTORY_MAX) % HISTORY_MAX;
        if (strcmp(history_buf[last], line) == 0)
            return;
    }

    // copy into the ring buffer
    strncpy(history_buf[history_end], line, LINE_MAX - 1);
    history_buf[history_end][LINE_MAX - 1] = '\0';

    history_end = (history_end + 1) % HISTORY_MAX;

    if (history_size < HISTORY_MAX)
    {
        history_size++;
    }
    else
    {
        // buffer is full: oldest entry gets overwritten
        history_start = (history_start + 1) % HISTORY_MAX;
    }
}

int history_count(void)
{
    return history_size;
}

const char *history_get(int index)
{
    if (index < 0 || index >= history_size)
        return NULL;

    int real_index = (history_start + index) % HISTORY_MAX;
    return history_buf[real_index];
}

const char *history_prev(const char *current)
{
    if (history_size == 0)
        return NULL;

    if (nav_pos == -1)
    {
        // first time pressing up: save current line
        strncpy(nav_saved, current, LINE_MAX - 1);
        nav_saved[LINE_MAX - 1] = '\0';
        nav_pos = history_size - 1;
    }
    else if (nav_pos > 0)
    {
        nav_pos--;
    }
    else
    {
        // already at the oldest entry
        return NULL;
    }

    return history_get(nav_pos);
}

const char *history_next(void)
{
    if (nav_pos == -1)
        return NULL;

    nav_pos++;

    if (nav_pos >= history_size)
    {
        // back to the current (unsaved) line
        nav_pos = -1;
        return nav_saved;
    }

    return history_get(nav_pos);
}

void history_reset_nav(void)
{
    nav_pos = -1;
}
