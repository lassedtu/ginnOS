#pragma once

/**
 * @file pat.h
 * @brief the page attribute table: write-combining for the framebuffer.
 */

#include "common/stdint.h"

/**
 * program the PAT so that a page with PWT set and PCD clear is
 * write-combining. call once at boot, before such pages are mapped.
 */
void pat_init(void);

/**
 * true if pat_init could set up write-combining (the CPU has a PAT).
 */
bool pat_has_write_combining(void);
