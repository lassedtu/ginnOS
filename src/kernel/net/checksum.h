#pragma once

/**
 * @file checksum.h
 * @brief the Internet checksum (RFC 1071): the ones' complement of the
 *        ones' complement sum of the 16-bit words.
 *
 * the words are added in memory order, so the result can be stored as it
 * is, in either byte order.
 */

#include "common/stdint.h"

/**
 * add bytes to a partial sum. an odd length pads with a zero byte, so
 * only the last piece of a checksum may have an odd length.
 */
uint32_t checksum_add(uint32_t sum, const void *data, uint32_t length);

/**
 * fold a partial sum and complement it: the checksum to store.
 */
uint16_t checksum_finish(uint32_t sum);

/**
 * the checksum of one buffer.
 */
uint16_t checksum(const void *data, uint32_t length);
