/**
 * @file layouts.c
 * @brief the list of built-in keyboard layouts, and the active one.
 *
 * the keyboard interrupt reads the active index on every key press; a
 * switch writes it once, so no lock is needed.
 */

#include "keyboard_layout.h"

#include "common/stddef.h"

typedef const keyboard_layout_t *(*layout_fn_t)(void);

// the first entry is the default
static const layout_fn_t layouts[] = {
    layout_dk_mac,
    layout_us,
};

#define LAYOUT_COUNT (sizeof(layouts) / sizeof(layouts[0]))

static volatile uint32_t active;

const keyboard_layout_t *keyboard_get_layout(void)
{
    return layouts[active]();
}

const keyboard_layout_t *keyboard_layout_at(uint32_t index)
{
    return index < LAYOUT_COUNT ? layouts[index]() : NULL;
}

uint32_t keyboard_layout_index(void)
{
    return active;
}

bool keyboard_set_layout(uint32_t index)
{
    if (index >= LAYOUT_COUNT)
    {
        return false;
    }
    active = index;
    return true;
}
