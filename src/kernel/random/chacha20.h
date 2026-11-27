#pragma once

/**
 * @file chacha20.h
 * @brief the ChaCha20 block function (RFC 8439 section 2.3).
 */

#include "common/stdint.h"

#define CHACHA20_KEY_SIZE   32
#define CHACHA20_BLOCK_SIZE 64

/**
 * make one 64-byte block of key stream.
 * @param key 32 bytes.
 * @param counter the block counter.
 * @param nonce 12 bytes.
 * @param out receives 64 bytes.
 */
void chacha20_block(const uint8_t key[CHACHA20_KEY_SIZE], uint32_t counter,
                    const uint8_t nonce[12], uint8_t out[CHACHA20_BLOCK_SIZE]);
