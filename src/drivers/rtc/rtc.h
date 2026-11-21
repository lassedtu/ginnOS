#pragma once

/**
 * @file rtc.h
 * @brief the CMOS real-time clock: the wall-clock time at boot.
 */

#include "common/stdint.h"

/**
 * read the date and time from the CMOS clock (taken as UTC).
 * @param seconds_out receives the seconds since 1970-01-01 00:00 UTC.
 * @return false if the clock gives no valid date.
 */
bool rtc_read_unix_time(uint32_t *seconds_out);
