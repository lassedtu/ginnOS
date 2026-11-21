#include "ktest.h"

#include "common/memory.h"
#include "kernel/fs/block_cache.h"
#include "kernel/memory/heap.h"

// a RAM disk big enough to overflow one cache set several times
#define RAM_SECTORS (BLOCK_CACHE_SETS * (BLOCK_CACHE_WAYS + 2))

static uint8_t *ram;
static uint32_t device_reads; // sectors the cache asked the device for

static bool ram_read(block_device_t *device, uint32_t start, uint8_t count, void *dest)
{
    (void)device;
    memcpy(dest, ram + start * BLOCK_CACHE_SECTOR, count * BLOCK_CACHE_SECTOR);
    device_reads += count;
    return true;
}

static bool ram_write(block_device_t *device, uint32_t start, uint8_t count, const void *src)
{
    (void)device;
    memcpy(ram + start * BLOCK_CACHE_SECTOR, src, count * BLOCK_CACHE_SECTOR);
    return true;
}

static block_device_t ram_device = {
    .bytes_per_block = BLOCK_CACHE_SECTOR,
    .read_blocks = ram_read,
    .write_blocks = ram_write,
};

static block_cache_t cache;
static uint8_t sector[2 * BLOCK_CACHE_SECTOR];

KTEST(block_cache_hits_and_writes_through)
{
    ram = kcalloc(RAM_SECTORS, BLOCK_CACHE_SECTOR);
    KEXPECT(ram != NULL);
    if (!ram)
        return;
    for (uint32_t i = 0; i < RAM_SECTORS; i++)
        ram[i * BLOCK_CACHE_SECTOR] = (uint8_t)i;

    KEXPECT(kerr_ok(block_cache_init(&cache, &ram_device)));
    device_reads = 0;

    // the first read goes to the device, the second does not
    KEXPECT(block_device_read(&cache.block, 5, 2, sector));
    KEXPECT_EQ(sector[0], 5);
    KEXPECT_EQ(sector[BLOCK_CACHE_SECTOR], 6);
    KEXPECT(block_device_read(&cache.block, 5, 2, sector));
    KEXPECT_EQ(device_reads, 2);
    KEXPECT_EQ(cache.hits, 2);

    // a write reaches the device at once, and a cached read sees it
    memset(sector, 0xAB, BLOCK_CACHE_SECTOR);
    KEXPECT(block_device_write(&cache.block, 6, 1, sector));
    KEXPECT_EQ(ram[6 * BLOCK_CACHE_SECTOR], 0xAB);
    KEXPECT(block_device_read(&cache.block, 6, 1, sector));
    KEXPECT_EQ(sector[0], 0xAB);
    KEXPECT_EQ(device_reads, 2);

    // more sectors of one set than it has ways: the least recently used
    // goes, the recently used one stays
    uint32_t set = 7;
    for (uint32_t way = 0; way < BLOCK_CACHE_WAYS; way++)
        block_device_read(&cache.block, set + way * BLOCK_CACHE_SETS, 1, sector);
    block_device_read(&cache.block, set, 1, sector); // used again: newest
    device_reads = 0;
    block_device_read(&cache.block, set + BLOCK_CACHE_WAYS * BLOCK_CACHE_SETS, 1, sector);
    KEXPECT_EQ(device_reads, 1);
    block_device_read(&cache.block, set, 1, sector);
    KEXPECT_EQ(device_reads, 1);                     // still cached
    block_device_read(&cache.block, set + BLOCK_CACHE_SETS, 1, sector);
    KEXPECT_EQ(device_reads, 2);                     // the one evicted
    KEXPECT_EQ(sector[0], (uint8_t)(set + BLOCK_CACHE_SETS));

    kfree(cache.data);
    kfree(ram);
}
