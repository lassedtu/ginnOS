#include "block_cache.h"

#include "common/memory.h"
#include "kernel/memory/heap.h"

block_cache_t *block_cache_root;

/**
 * the bytes of a slot.
 */
static uint8_t *slot_data(block_cache_t *cache, uint32_t set, uint32_t way)
{
    return cache->data + ((set * BLOCK_CACHE_WAYS) + way) * BLOCK_CACHE_SECTOR;
}

/**
 * the way that holds lba in its set, or -1.
 */
static int find_way(block_cache_t *cache, uint32_t set, uint32_t lba)
{
    for (int way = 0; way < BLOCK_CACHE_WAYS; way++)
    {
        block_cache_slot_t *slot = &cache->slots[set][way];
        if (slot->valid && slot->lba == lba)
        {
            return way;
        }
    }
    return -1;
}

/**
 * the way to fill for lba: a free one, else the least recently used.
 */
static int victim_way(block_cache_t *cache, uint32_t set)
{
    int oldest = 0;
    for (int way = 0; way < BLOCK_CACHE_WAYS; way++)
    {
        block_cache_slot_t *slot = &cache->slots[set][way];
        if (!slot->valid)
        {
            return way;
        }
        if (slot->last_use < cache->slots[set][oldest].last_use)
        {
            oldest = way;
        }
    }
    return oldest;
}

/**
 * copy one sector into the cache (a new slot, or the one already there).
 */
static void store(block_cache_t *cache, uint32_t lba, const uint8_t *sector)
{
    uint32_t set = lba % BLOCK_CACHE_SETS;
    int way = find_way(cache, set, lba);
    if (way < 0)
    {
        way = victim_way(cache, set);
    }

    block_cache_slot_t *slot = &cache->slots[set][way];
    slot->lba = lba;
    slot->valid = true;
    slot->last_use = ++cache->use_counter;
    memcpy(slot_data(cache, set, (uint32_t)way), sector, BLOCK_CACHE_SECTOR);
}

static bool cache_read(block_device_t *device, uint32_t start, uint8_t count, void *dest)
{
    block_cache_t *cache = device->context;
    uint8_t *out = dest;

    mutex_lock(&cache->lock);

    // all cached: copy them out
    bool all_cached = true;
    for (uint32_t i = 0; i < count && all_cached; i++)
    {
        all_cached = find_way(cache, (start + i) % BLOCK_CACHE_SETS, start + i) >= 0;
    }

    bool ok = true;
    if (all_cached)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            uint32_t lba = start + i;
            uint32_t set = lba % BLOCK_CACHE_SETS;
            int way = find_way(cache, set, lba);
            cache->slots[set][way].last_use = ++cache->use_counter;
            memcpy(out + i * BLOCK_CACHE_SECTOR, slot_data(cache, set, (uint32_t)way),
                   BLOCK_CACHE_SECTOR);
        }
        cache->hits += count;
    }
    else
    {
        // one read for the whole range, then keep every sector of it
        ok = block_device_read(cache->backing, start, count, dest);
        if (ok)
        {
            for (uint32_t i = 0; i < count; i++)
            {
                store(cache, start + i, out + i * BLOCK_CACHE_SECTOR);
            }
            cache->misses += count;
        }
    }

    mutex_unlock(&cache->lock);
    return ok;
}

static bool cache_write(block_device_t *device, uint32_t start, uint8_t count, const void *src)
{
    block_cache_t *cache = device->context;
    const uint8_t *in = src;

    mutex_lock(&cache->lock);
    bool ok = block_device_write(cache->backing, start, count, src);
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t lba = start + i;
        if (ok)
        {
            store(cache, lba, in + i * BLOCK_CACHE_SECTOR);
        }
        else
        {
            // the disk may hold either version now: forget the sector
            uint32_t set = lba % BLOCK_CACHE_SETS;
            int way = find_way(cache, set, lba);
            if (way >= 0)
            {
                cache->slots[set][way].valid = false;
            }
        }
    }
    mutex_unlock(&cache->lock);
    return ok;
}

static bool cache_flush(block_device_t *device)
{
    // write-through: the disk is never behind the cache
    block_cache_t *cache = device->context;
    return block_device_flush(cache->backing);
}

kerr_t block_cache_init(block_cache_t *cache, block_device_t *backing)
{
    if (!backing || backing->bytes_per_block != BLOCK_CACHE_SECTOR)
    {
        return KERR_INVAL;
    }

    cache->data = kmalloc(BLOCK_CACHE_SETS * BLOCK_CACHE_WAYS * BLOCK_CACHE_SECTOR);
    if (!cache->data)
    {
        return KERR_NOMEM;
    }

    memset(cache->slots, 0, sizeof(cache->slots));
    cache->backing = backing;
    cache->use_counter = 0;
    cache->hits = 0;
    cache->misses = 0;
    mutex_init(&cache->lock);

    cache->block = *backing;
    cache->block.context = cache;
    cache->block.read_blocks = cache_read;
    cache->block.write_blocks = cache_write;
    cache->block.flush = cache_flush;
    return KERR_OK;
}
