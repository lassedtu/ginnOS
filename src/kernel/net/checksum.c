/**
 * @file checksum.c
 * @brief the Internet checksum (see checksum.h).
 */

#include "checksum.h"

uint32_t checksum_add(uint32_t sum, const void *data, uint32_t length)
{
    const uint8_t *p = data;
    while (length >= 2)
    {
        sum += (uint32_t)(p[0] | (p[1] << 8));
        p += 2;
        length -= 2;
        if (sum & 0x80000000u)
        {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
    }
    if (length)
    {
        sum += p[0];
    }
    return sum;
}

uint16_t checksum_finish(uint32_t sum)
{
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

uint16_t checksum(const void *data, uint32_t length)
{
    return checksum_finish(checksum_add(0, data, length));
}
