#pragma once

/**
 * 64-bit by 32-bit division. the freestanding 32-bit build has no
 * __udivdi3, so 64-bit "/" and "%" do not link; use this instead.
 */

#include "common/stdint.h"

/**
 * divide a 64-bit number by a 32-bit one (shift and subtract).
 * @param dividend the number to divide.
 * @param divisor the divisor (not 0).
 * @param remainder_out receives the remainder (may be NULL).
 * @return the quotient.
 */
static inline uint64_t udiv64_32(uint64_t dividend, uint32_t divisor, uint32_t *remainder_out)
{
    uint64_t quotient = 0;
    uint64_t remainder = 0;

    for (int bit = 63; bit >= 0; bit--)
    {
        remainder = (remainder << 1) | ((dividend >> bit) & 1ull);
        if (remainder >= divisor)
        {
            remainder -= divisor;
            quotient |= 1ull << bit;
        }
    }

    if (remainder_out)
    {
        *remainder_out = (uint32_t)remainder;
    }
    return quotient;
}
