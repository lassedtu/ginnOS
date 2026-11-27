#pragma once

/**
 * @file langinfo.h
 * @brief facts about the locale. ginnOS has the "C" locale with UTF-8
 *        text, so the answers are fixed.
 */

typedef int nl_item;

#define CODESET    0  // "UTF-8"
#define D_T_FMT    1  // "%a %b %e %H:%M:%S %Y"
#define D_FMT      2  // "%m/%d/%y"
#define T_FMT      3  // "%H:%M:%S"
#define T_FMT_AMPM 4  // "%I:%M:%S %p"
#define AM_STR     5  // "AM"
#define PM_STR     6  // "PM"
#define RADIXCHAR  7  // "."
#define THOUSEP    8  // ""
#define YESEXPR    9  // "^[yY]"
#define NOEXPR     10 // "^[nN]"

/**
 * @return the text for an item, or "" for an item it does not know.
 */
char *nl_langinfo(nl_item item);
