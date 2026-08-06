#ifndef PIT_H
#define PIT_H

#include "../../common/stdint.h"

/**
 * initializes the PIT.
 *
 * @param frequency Desired interrupt frequency in Hz.
 */
void pit_initialize(uint32_t frequency);

/**
 * returns the number of PIT interrupts since initialization.
 */
uint64_t pit_get_ticks(void);

#endif