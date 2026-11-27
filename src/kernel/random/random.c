/**
 * @file random.c
 * @brief the random pool and generator (see random.h).
 *
 * one spinlock (irqsave) keeps the pool and the generator. output is made
 * in CHUNK-byte parts under the lock into a kernel buffer, and copied to
 * user memory after the lock is let go: a copy can fault.
 */

#include "random.h"

#include "arch/arch.h"
#include "blake2s.h"
#include "chacha20.h"
#include "common/memory.h"
#include "common/string.h"
#include "kernel/device/device.h"
#include "kernel/errno.h"
#include "kernel/klog/klog.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"
#include "kernel/sync/spinlock.h"
#include "kernel/sync/wait_queue.h"
#include "kernel/time/clock.h"
#include "kernel/time/timer.h"
#include "kernel/usermode/uaccess.h"

#define CHUNK            256
#define RESEED_MS        1000
#define JITTER_SAMPLES   4096
#define JITTER_PER_BIT   16 // samples for each bit of entropy credited
#define FAST_POOL_EVENTS 64
#define GETRANDOM_MAX    (32u * 1024 * 1024)

static spinlock_t lock;
static blake2s_t pool;
static uint32_t pool_bits;  // entropy in the pool since the last reseed
static uint32_t total_bits; // entropy credited since boot (stops at the seed level)
static bool pool_has_input;
static uint8_t key[CHACHA20_KEY_SIZE];
static uint64_t generation; // the nonce: a key is never used twice with one
static uint64_t last_reseed;
static bool seeded;
static wait_queue_t seed_waiters;

// the fast pool of the interrupts (only touched with interrupts off)
static uint32_t fast_pool[4];
static uint32_t fast_events;

static device_t random_device;
static device_t urandom_device;

bool random_seeded(void)
{
    return seeded;
}

/**
 * the pool's hash becomes part of a new key (lock held).
 */
static void reseed(void)
{
    uint8_t digest[BLAKE2S_HASH_SIZE];
    blake2s_final(&pool, digest);
    blake2s_t mixer;
    blake2s_init(&mixer, CHACHA20_KEY_SIZE);
    blake2s_update(&mixer, key, sizeof(key));
    blake2s_update(&mixer, digest, sizeof(digest));
    blake2s_final(&mixer, key);
    // the next pool starts from this one, so no input is lost
    blake2s_init(&pool, BLAKE2S_HASH_SIZE);
    blake2s_update(&pool, digest, sizeof(digest));
    memset(digest, 0, sizeof(digest));
    pool_bits = 0;
    pool_has_input = false;
    last_reseed = timer_now();
}

/**
 * credit entropy, and reseed at once when it makes the generator seeded
 * (lock held).
 */
static void credit(uint32_t bits)
{
    pool_bits += bits;
    if (seeded)
    {
        return;
    }
    total_bits += bits;
    if (total_bits >= RANDOM_SEED_BITS)
    {
        reseed();
        seeded = true;
        wait_queue_wake_all(&seed_waiters);
    }
}

void random_add(const void *data, uint32_t length, uint32_t entropy_bits)
{
    uint32_t flags = spin_lock_irqsave(&lock);
    blake2s_update(&pool, data, length);
    pool_has_input = true;
    credit(entropy_bits);
    spin_unlock_irqrestore(&lock, flags);
}

/**
 * make length (at most CHUNK) bytes (lock held).
 */
static void generate(uint8_t *out, uint32_t length)
{
    if (pool_has_input && timer_now() - last_reseed >= timer_ms_to_ticks(RESEED_MS))
    {
        reseed();
    }
    uint8_t nonce[12] = {0};
    memcpy(nonce, &generation, sizeof(generation));
    generation++;
    // block 0 gives the next key; the blocks after it are the output
    uint8_t block[CHACHA20_BLOCK_SIZE];
    chacha20_block(key, 0, nonce, block);
    uint8_t next_key[CHACHA20_KEY_SIZE];
    memcpy(next_key, block, sizeof(next_key));
    uint32_t at = 0;
    for (uint32_t counter = 1; at < length; counter++)
    {
        chacha20_block(key, counter, nonce, block);
        uint32_t n = length - at < CHACHA20_BLOCK_SIZE ? length - at : CHACHA20_BLOCK_SIZE;
        memcpy(out + at, block, n);
        at += n;
    }
    memcpy(key, next_key, sizeof(key));
    memset(next_key, 0, sizeof(next_key));
    memset(block, 0, sizeof(block));
}

void random_get(void *out, uint32_t length)
{
    uint8_t *p = out;
    while (length > 0)
    {
        uint32_t n = length < CHUNK ? length : CHUNK;
        uint32_t flags = spin_lock_irqsave(&lock);
        generate(p, n);
        spin_unlock_irqrestore(&lock, flags);
        p += n;
        length -= n;
    }
}

uint32_t random_u32(void)
{
    uint32_t value;
    random_get(&value, sizeof(value));
    return value;
}

void random_add_interrupt(uint32_t irq)
{
    uint64_t cycles = arch_cycle_count();
    // a small add-rotate-xor mix; the pool hashes it properly later
    fast_pool[0] += (uint32_t)cycles;
    fast_pool[1] ^= (uint32_t)(cycles >> 32) + irq;
    fast_pool[2] += fast_pool[1] << 7 | fast_pool[1] >> 25;
    fast_pool[3] ^= fast_pool[0] << 13 | fast_pool[0] >> 19;
    fast_pool[0] += fast_pool[3];
    fast_pool[1] += fast_pool[2] << 11 | fast_pool[2] >> 21;
    if (++fast_events < FAST_POOL_EVENTS)
    {
        return;
    }
    fast_events = 0;
    // one bit for each 64 interrupts until seeded, as Linux; more is
    // credited only to make a machine without a CPU generator start sooner
    random_add(fast_pool, sizeof(fast_pool), seeded ? 1 : 4);
}

/**
 * CPU timing jitter: the cycle counter around a small piece of work
 * changes by amounts no one can see in advance.
 */
static void add_jitter(void)
{
    uint32_t samples[64];
    uint32_t work = 0;
    for (uint32_t i = 0; i < JITTER_SAMPLES; i++)
    {
        uint64_t start = arch_cycle_count();
        for (uint32_t j = 0; j < (start & 15) + 8; j++)
        {
            work = work * 1103515245u + 12345u + (uint32_t)j;
        }
        samples[i % 64] = (uint32_t)(arch_cycle_count() - start) ^ work;
        if (i % 64 == 63)
        {
            random_add(samples, sizeof(samples), 64 / JITTER_PER_BIT);
        }
    }
}

static uint32_t random_read(device_t *dev, uint32_t offset, void *buf, uint32_t len)
{
    (void)dev;
    (void)offset;
    uint8_t chunk[CHUNK];
    uint32_t done = 0;
    while (done < len && !signal_pending(process_current()))
    {
        uint32_t n = len - done < CHUNK ? len - done : CHUNK;
        random_get(chunk, n);
        memcpy((uint8_t *)buf + done, chunk, n);
        done += n;
    }
    memset(chunk, 0, sizeof(chunk));
    return done;
}

static uint32_t random_write(device_t *dev, uint32_t offset, const void *buf, uint32_t len)
{
    (void)dev;
    (void)offset;
    // mixed in, with no entropy credited (as Linux)
    uint8_t chunk[CHUNK];
    for (uint32_t done = 0; done < len;)
    {
        uint32_t n = len - done < CHUNK ? len - done : CHUNK;
        memcpy(chunk, (const uint8_t *)buf + done, n);
        random_add(chunk, n, 0);
        done += n;
    }
    return len;
}

static const device_ops_t random_ops = {
    .read = random_read,
    .write = random_write,
};

static void add_device(device_t *dev, const char *name)
{
    strncpy(dev->name, name, DEVICE_NAME_MAX - 1);
    dev->name[DEVICE_NAME_MAX - 1] = '\0';
    dev->type = DEVICE_TYPE_CHAR;
    dev->ops = &random_ops;
    dev->driver_data = NULL;
    device_register(dev);
}

void random_init(void)
{
    spinlock_init(&lock);
    wait_queue_init(&seed_waiters);
    blake2s_init(&pool, BLAKE2S_HASH_SIZE);

    // what makes this boot different: the time and the cycle counter
    struct ktimespec now;
    clock_realtime(&now);
    uint64_t cycles = arch_cycle_count();
    random_add(&now, sizeof(now), 0);
    random_add(&cycles, sizeof(cycles), 0);

    // the CPU's generator: the full seed at once
    uintptr_t word;
    uint32_t words = 0;
    for (uint32_t bits = 0; bits < 2 * RANDOM_SEED_BITS && arch_hw_random(&word);
         bits += 8 * sizeof(word))
    {
        random_add(&word, sizeof(word), 8 * sizeof(word));
        words++;
    }
    add_jitter();
    uint32_t flags = spin_lock_irqsave(&lock);
    reseed(); // the first key, seeded or not
    spin_unlock_irqrestore(&lock, flags);

    add_device(&random_device, "random");
    add_device(&urandom_device, "urandom");
    KLOG_INFO("random: %s, %u words from the CPU", seeded ? "seeded" : "not seeded yet", words);
}

int32_t random_getrandom(void *user_buffer, uint32_t length, uint32_t flags)
{
    if (flags & ~(uint32_t)(GRND_NONBLOCK | GRND_RANDOM | GRND_INSECURE))
    {
        return -EINVAL;
    }
    if ((flags & GRND_RANDOM) && (flags & GRND_INSECURE))
    {
        return -EINVAL;
    }
    if (!seeded && !(flags & GRND_INSECURE))
    {
        if (flags & GRND_NONBLOCK)
        {
            return -EAGAIN;
        }
        uint32_t irq_flags = spin_lock_irqsave(&lock);
        while (!seeded && !signal_pending(process_current()))
        {
            wait_queue_block_locked_interruptible(&seed_waiters, &lock);
        }
        spin_unlock_irqrestore(&lock, irq_flags);
        if (!seeded)
        {
            return -EINTR;
        }
    }
    if (length > GETRANDOM_MAX)
    {
        length = GETRANDOM_MAX;
    }
    if (!user_access_ok(user_buffer, length, true))
    {
        return -EFAULT;
    }
    uint8_t chunk[CHUNK];
    uint32_t done = 0;
    while (done < length)
    {
        // a long request stops at a signal with what it has
        if (done > 0 && signal_pending(process_current()))
        {
            break;
        }
        uint32_t n = length - done < CHUNK ? length - done : CHUNK;
        random_get(chunk, n);
        if (kerr_failed(copy_to_user((uint8_t *)user_buffer + done, chunk, n)))
        {
            memset(chunk, 0, sizeof(chunk));
            return done > 0 ? (int32_t)done : -EFAULT;
        }
        done += n;
    }
    memset(chunk, 0, sizeof(chunk));
    return (int32_t)done;
}
