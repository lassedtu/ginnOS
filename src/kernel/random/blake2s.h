#pragma once

/**
 * @file blake2s.h
 * @brief the BLAKE2s hash (RFC 7693), without a key. the random pool uses
 *        it to mix its input.
 */

#include "common/stdint.h"

#define BLAKE2S_BLOCK_SIZE 64
#define BLAKE2S_HASH_SIZE  32

typedef struct
{
    uint32_t h[8];
    uint32_t t[2]; // the bytes hashed so far
    uint8_t buffer[BLAKE2S_BLOCK_SIZE];
    uint32_t buffered;
    uint32_t out_length;
} blake2s_t;

/**
 * start a hash.
 * @param out_length the hash size, 1 to BLAKE2S_HASH_SIZE bytes.
 */
void blake2s_init(blake2s_t *s, uint32_t out_length);

void blake2s_update(blake2s_t *s, const void *data, uint32_t length);

/**
 * finish the hash. the state must be started again before more use.
 */
void blake2s_final(blake2s_t *s, uint8_t *out);
