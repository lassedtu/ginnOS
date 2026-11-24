#pragma once

/**
 * @file random.h
 * @brief random numbers for the kernel and for programs: /dev/urandom,
 *        /dev/random and the getrandom call.
 *
 * the pool: input (the CPU's RDSEED or RDRAND, the cycle counter at each
 * interrupt, the time at boot, CPU timing jitter, data written to
 * /dev/urandom) goes into a BLAKE2s hash. the generator: ChaCha20 with a
 * 256-bit key, made new from the pool's hash at most once a second when
 * there is new input. after each request the generator takes a new key
 * from its own output, so a later look at the state cannot give back
 * what it made before (fast key erasure).
 *
 * the generator is "seeded" when the pool has had RANDOM_SEED_BITS bits
 * of entropy (an estimate). the CPU's generator gives them at once; else
 * the jitter at boot and the interrupts do. getrandom waits for that
 * unless GRND_NONBLOCK; /dev/urandom does not wait.
 */

#include "common/stdint.h"

#define RANDOM_SEED_BITS 256

// getrandom flags (the same as Linux's)
#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM   0x0002
#define GRND_INSECURE 0x0004

/**
 * fill the pool from the CPU, the clock and timing jitter, and add the
 * devices random and urandom. call once, before interrupts are on.
 */
void random_init(void);

/**
 * random bytes for the kernel. they are good once random_seeded() is
 * true; before that they come from what the pool has.
 */
void random_get(void *out, uint32_t length);

/**
 * a random 32-bit value (random_get of 4 bytes).
 */
uint32_t random_u32(void);

/**
 * add data to the pool, with an estimate of its entropy.
 */
void random_add(const void *data, uint32_t length, uint32_t entropy_bits);

/**
 * an interrupt came (from the interrupt dispatcher): the cycle counter
 * goes into a small fast pool, which goes into the pool every 64
 * interrupts.
 */
void random_add_interrupt(uint32_t irq);

/**
 * true when the pool has had enough entropy for keys.
 */
bool random_seeded(void);

/**
 * the getrandom call: fill user memory.
 * @return the bytes given, or -EAGAIN (not seeded, GRND_NONBLOCK),
 *         -EINVAL (unknown flags), -EFAULT, -EINTR.
 */
int32_t random_getrandom(void *user_buffer, uint32_t length, uint32_t flags);
