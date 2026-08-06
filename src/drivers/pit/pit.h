#pragma once

#include "../../common/stdint.h"

/**
 * initializes the PIT timer.
 *
 * @param frequency desired interrupt frequency in Hz.
 */
void pit_initialize(uint32_t frequency);

/**
 * returns the number of PIT interrupts since initialization.
 *
 * @return number of timer ticks since PIT initialization.
 */
uint64_t pit_get_ticks(void);