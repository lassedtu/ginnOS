#pragma once

/**
 * @file block_cache.h
 * @brief a sector cache in front of a block device.
 *
 * a block_cache_t is itself a block device: reads are served from memory
 * when the sectors are cached; writes go to the device at once (write
 * through) and update the cached copies, so the disk is never behind.
 *
 * the cache holds BLOCK_CACHE_SETS * BLOCK_CACHE_WAYS sectors. a sector can
 * be in one of the BLOCK_CACHE_WAYS slots of its set (lba % sets); a miss
 * replaces the least recently used slot of the set.
 */

#include "common/error.h"
#include "drivers/disk/block_device.h"
#include "kernel/sync/mutex.h"

#define BLOCK_CACHE_SETS 256
#define BLOCK_CACHE_WAYS 4
#define BLOCK_CACHE_SECTOR 512

typedef struct
{
    uint32_t lba;
    uint32_t last_use; // value of the use counter at the last access
    bool valid;
} block_cache_slot_t;

typedef struct
{
    block_device_t block;     // the cached device (hand this to the filesystem)
    block_device_t *backing;  // the device under the cache
    block_cache_slot_t slots[BLOCK_CACHE_SETS][BLOCK_CACHE_WAYS];
    uint8_t *data;            // the sectors, slot by slot
    uint32_t use_counter;
    uint32_t hits;            // sectors served from memory
    uint32_t misses;          // sectors read from the device
    mutex_t lock;
} block_cache_t;

/**
 * put a cache in front of a device with 512-byte sectors.
 * @param cache the cache (lives as long as the device is used).
 * @param backing the device to cache.
 * @return KERR_OK, KERR_INVAL for another sector size, or KERR_NOMEM.
 */
kerr_t block_cache_init(block_cache_t *cache, block_device_t *backing);

/**
 * the cache that serves the root filesystem, for /proc/meminfo (NULL
 * before it is set up).
 */
extern block_cache_t *block_cache_root;
