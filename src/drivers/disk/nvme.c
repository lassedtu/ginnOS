/**
 * the NVMe driver (NVM Express base specification 1.4).
 *
 * the host puts 64-byte commands into a submission queue and writes the new
 * tail to the queue's doorbell. the controller puts a 16-byte entry into
 * the completion queue for each command; the phase bit of an entry changes
 * each time the queue wraps, so the host sees which entries are new.
 *
 * queue 0 is the admin queue (identify, create the I/O queues). queue 1 is
 * the one I/O queue. each queue is one page. the data goes through a bounce
 * page (one PRP entry, 8 sectors), because a caller's buffer can be on a
 * kernel stack, which is not physically contiguous.
 *
 * locking: one command at a time, with interrupts off while it runs.
 */

#include "nvme.h"

#include "arch/arch.h"
#include "common/memory.h"
#include "drivers/pci/pci.h"
#include "kernel/klog/klog.h"
#include "kernel/memory/mmio.h"
#include "kernel/memory/page.h"
#include "kernel/memory/pmm.h"

// controller registers
#define REG_CAP  0x00 // 64 bits
#define REG_CC   0x14
#define REG_CSTS 0x1C
#define REG_AQA  0x24
#define REG_ASQ  0x28 // 64 bits
#define REG_ACQ  0x30 // 64 bits
#define REG_DOORBELLS 0x1000

#define CC_ENABLE   0x1u
#define CC_IOSQES   (6u << 16) // submission entries are 2^6 bytes
#define CC_IOCQES   (4u << 20) // completion entries are 2^4 bytes
#define CSTS_READY  0x1u
#define CSTS_FATAL  0x2u

// opcodes
#define ADMIN_CREATE_SQ 0x01
#define ADMIN_CREATE_CQ 0x05
#define ADMIN_IDENTIFY  0x06
#define IO_FLUSH        0x00
#define IO_WRITE        0x01
#define IO_READ         0x02

#define QUEUE_ENTRIES 16
#define SECTOR_SIZE   512u
#define BOUNCE_SECTORS (PAGE_SIZE / SECTOR_SIZE)
#define SPIN_LIMIT    100000000u

typedef struct
{
    uint8_t *sq;      // submission queue (QUEUE_ENTRIES x 64 bytes)
    uint8_t *cq;      // completion queue (QUEUE_ENTRIES x 16 bytes)
    uint16_t id;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint8_t phase;    // the phase bit of new completion entries
    uint16_t next_cid;
} queue_t;

static volatile uint8_t *regs;
static uint32_t doorbell_stride; // bytes between doorbells
static queue_t admin;
static queue_t io;
static uint8_t *bounce;
static nvme_disk_t disk;

static uint32_t reg_read32(uint32_t reg)
{
    return *(volatile uint32_t *)(regs + reg);
}

static void reg_write32(uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(regs + reg) = value;
}

static uint64_t reg_read64(uint32_t reg)
{
    return (uint64_t)reg_read32(reg) | ((uint64_t)reg_read32(reg + 4) << 32);
}

static void reg_write64(uint32_t reg, uint64_t value)
{
    reg_write32(reg, (uint32_t)value);
    reg_write32(reg + 4, (uint32_t)(value >> 32));
}

static void put32(uint8_t *p, uint32_t value)
{
    memcpy(p, &value, sizeof(value));
}

static void put64(uint8_t *p, uint64_t value)
{
    memcpy(p, &value, sizeof(value));
}

static uint32_t get32(const uint8_t *p)
{
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static uint8_t *alloc_page(void)
{
    uint8_t *page = pmm_alloc_page();
    if (page)
    {
        memset(page, 0, PAGE_SIZE);
    }
    return page;
}

static bool queue_init(queue_t *queue, uint16_t id)
{
    queue->sq = alloc_page();
    queue->cq = alloc_page();
    queue->id = id;
    queue->sq_tail = 0;
    queue->cq_head = 0;
    queue->phase = 1;
    queue->next_cid = 0;
    return queue->sq && queue->cq;
}

/**
 * submit one command and wait for its completion entry.
 * @param command the 64-byte command; the command id is set here.
 * @param result receives dword 0 of the completion (may be NULL).
 * @return false on an error status or a timeout.
 */
static bool submit(queue_t *queue, uint8_t *command, uint32_t *result)
{
    uint16_t cid = queue->next_cid++;
    command[2] = (uint8_t)cid;
    command[3] = (uint8_t)(cid >> 8);

    memcpy(queue->sq + queue->sq_tail * 64, command, 64);
    queue->sq_tail = (uint16_t)((queue->sq_tail + 1) % QUEUE_ENTRIES);
    reg_write32(REG_DOORBELLS + (2u * queue->id) * doorbell_stride, queue->sq_tail);

    volatile uint8_t *entry = queue->cq + queue->cq_head * 16;
    for (uint32_t spin = 0; ((entry[14] & 1u) != queue->phase); spin++)
    {
        if (spin > SPIN_LIMIT || (reg_read32(REG_CSTS) & CSTS_FATAL))
        {
            return false;
        }
    }

    uint32_t dw0 = get32((const uint8_t *)entry);
    uint16_t status = (uint16_t)((entry[14] | (entry[15] << 8)) >> 1);

    queue->cq_head = (uint16_t)((queue->cq_head + 1) % QUEUE_ENTRIES);
    if (queue->cq_head == 0)
    {
        queue->phase ^= 1u;
    }
    reg_write32(REG_DOORBELLS + (2u * queue->id + 1) * doorbell_stride, queue->cq_head);

    if (result)
    {
        *result = dw0;
    }
    return status == 0;
}

static void command_init(uint8_t *command, uint8_t opcode, uint32_t nsid, phys_addr_t prp1)
{
    memset(command, 0, 64);
    command[0] = opcode;
    put32(command + 4, nsid);
    put64(command + 24, prp1);
}

static bool nvme_transfer(nvme_disk_t *d, uint32_t start, uint8_t count, void *buffer, bool write)
{
    if ((uint64_t)start + count > d->sectors)
    {
        return false;
    }

    uint8_t *data = buffer;
    bool ok = true;
    uint32_t flags = arch_irq_save();
    while (count > 0 && ok)
    {
        uint32_t chunk = count < BOUNCE_SECTORS ? count : BOUNCE_SECTORS;
        uint32_t bytes = chunk * SECTOR_SIZE;
        if (write)
        {
            memcpy(bounce, data, bytes);
        }

        uint8_t command[64];
        command_init(command, write ? IO_WRITE : IO_READ, 1, virt_to_phys(bounce));
        put64(command + 40, start);    // the first LBA (dwords 10 and 11)
        put32(command + 48, chunk - 1); // the number of blocks - 1
        ok = submit(&io, command, NULL);

        if (ok && !write)
        {
            memcpy(data, bounce, bytes);
        }
        data += bytes;
        start += chunk;
        count = (uint8_t)(count - chunk);
    }
    arch_irq_restore(flags);
    return ok;
}

static bool nvme_read(block_device_t *device, uint32_t start, uint8_t count, void *dest)
{
    return nvme_transfer(device->context, start, count, dest, false);
}

static bool nvme_write(block_device_t *device, uint32_t start, uint8_t count, const void *src)
{
    return nvme_transfer(device->context, start, count, (void *)src, true);
}

static bool nvme_flush(block_device_t *device)
{
    (void)device;
    uint8_t command[64];
    command_init(command, IO_FLUSH, 1, 0);
    uint32_t flags = arch_irq_save();
    bool ok = submit(&io, command, NULL);
    arch_irq_restore(flags);
    return ok;
}

static bool wait_ready(bool ready, uint32_t timeout_units)
{
    // CAP.TO is in units of 500 ms; spin a generous count per unit
    for (uint64_t spin = 0; ((reg_read32(REG_CSTS) & CSTS_READY) != 0) != ready; spin++)
    {
        if (spin > (uint64_t)(timeout_units + 1) * 20000000u)
        {
            return false;
        }
    }
    return true;
}

/**
 * identify namespace 1: its size and sector size.
 */
static bool identify_namespace(void)
{
    uint8_t command[64];
    command_init(command, ADMIN_IDENTIFY, 1, virt_to_phys(bounce));
    put32(command + 40, 0); // CNS 0: the namespace
    if (!submit(&admin, command, NULL))
    {
        return false;
    }

    uint64_t size;
    memcpy(&size, bounce, sizeof(size));
    uint8_t format = bounce[26] & 0xF;
    uint32_t lbaf = get32(bounce + 128 + format * 4);
    uint32_t sector_shift = (lbaf >> 16) & 0xFF;
    if (size == 0 || sector_shift != 9)
    {
        KLOG_WARN("nvme: namespace 1 has %u-byte sectors; not used", 1u << sector_shift);
        return false;
    }
    disk.sectors = size;
    return true;
}

static bool create_io_queues(void)
{
    if (!queue_init(&io, 1))
    {
        return false;
    }

    uint8_t command[64];
    command_init(command, ADMIN_CREATE_CQ, 0, virt_to_phys(io.cq));
    put32(command + 40, ((uint32_t)(QUEUE_ENTRIES - 1) << 16) | io.id);
    put32(command + 44, 1); // physically contiguous, no interrupts
    if (!submit(&admin, command, NULL))
    {
        return false;
    }

    command_init(command, ADMIN_CREATE_SQ, 0, virt_to_phys(io.sq));
    put32(command + 40, ((uint32_t)(QUEUE_ENTRIES - 1) << 16) | io.id);
    put32(command + 44, ((uint32_t)io.id << 16) | 1); // its CQ, contiguous
    return submit(&admin, command, NULL);
}

nvme_disk_t *nvme_init(void)
{
    pci_device_t *dev = pci_find_class(PCI_CLASS_STORAGE, PCI_SUBCLASS_NVME, 0x02, 0);
    if (!dev || dev->bars[0].base == 0 || dev->bars[0].is_io ||
        (sizeof(phys_addr_t) < 8 && (dev->bars[0].base >> 32) != 0))
    {
        return NULL;
    }

    regs = ioremap_nocache((phys_addr_t)dev->bars[0].base, 0x2000);
    bounce = alloc_page();
    if (!regs || !bounce || !queue_init(&admin, 0))
    {
        return NULL;
    }
    pci_enable_bus_master(dev);

    uint64_t cap = reg_read64(REG_CAP);
    doorbell_stride = 4u << ((cap >> 32) & 0xF);
    uint32_t timeout = (uint32_t)((cap >> 24) & 0xFF);
    if (((cap >> 48) & 0xF) > 0)
    {
        KLOG_WARN("nvme: the controller does not take 4 KiB pages");
        return NULL;
    }

    // disable, give the admin queues, enable
    reg_write32(REG_CC, reg_read32(REG_CC) & ~CC_ENABLE);
    if (!wait_ready(false, timeout))
    {
        return NULL;
    }
    reg_write32(REG_AQA, ((uint32_t)(QUEUE_ENTRIES - 1) << 16) | (QUEUE_ENTRIES - 1));
    reg_write64(REG_ASQ, virt_to_phys(admin.sq));
    reg_write64(REG_ACQ, virt_to_phys(admin.cq));
    reg_write32(REG_CC, CC_ENABLE | CC_IOSQES | CC_IOCQES);
    if (!wait_ready(true, timeout))
    {
        KLOG_WARN("nvme: the controller did not become ready");
        return NULL;
    }

    uint32_t flags = arch_irq_save();
    bool ok = identify_namespace() && create_io_queues();
    arch_irq_restore(flags);
    if (!ok)
    {
        return NULL;
    }

    disk.block.bytes_per_block = SECTOR_SIZE;
    disk.block.total_blocks = disk.sectors > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)disk.sectors;
    disk.block.context = &disk;
    disk.block.read_blocks = nvme_read;
    disk.block.write_blocks = nvme_write;
    disk.block.flush = nvme_flush;
    KLOG_INFO("nvme: namespace 1: %u MiB", (uint32_t)(disk.sectors / 2048));
    return &disk;
}
