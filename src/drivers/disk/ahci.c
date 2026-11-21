/**
 * the AHCI driver (Serial ATA AHCI 1.3.1 specification).
 *
 * the HBA registers (ABAR) have a generic part and one 128-byte register
 * set per port from offset 0x100. a port reads its commands from a command
 * list (32 headers of 32 bytes); a header points to a command table with
 * the command FIS and the PRDT, the list of memory areas for the data. the
 * HBA writes received FISes into the FIS area.
 *
 * the driver uses one command slot per port and one page of RAM per port:
 *   0x000 command list (1 KiB aligned; only header 0 is used)
 *   0x400 received FIS area (256 bytes, 256-byte aligned)
 *   0x500 command table (128-byte aligned): the FIS, then one PRDT entry
 *   0x600 the IDENTIFY buffer and the start of the bounce buffer
 * the data goes through the bounce buffer, a second page, because a
 * caller's buffer can be on a kernel stack, which is not physically
 * contiguous.
 *
 * locking: one transfer at a time per disk, with interrupts off while the
 * command runs (the kernel is uniprocessor and the wait is short).
 */

#include "ahci.h"

#include "arch/arch.h"
#include "common/memory.h"
#include "drivers/pci/pci.h"
#include "kernel/klog/klog.h"
#include "kernel/memory/mmio.h"
#include "kernel/memory/page.h"
#include "kernel/memory/pmm.h"

// generic host control registers (byte offsets in ABAR)
#define HBA_CAP 0x00
#define HBA_GHC 0x04
#define HBA_PI  0x0C

#define CAP_S64A   (1u << 31) // 64-bit DMA addresses
#define GHC_AE     (1u << 31) // AHCI mode
#define PORT_BASE  0x100
#define PORT_SIZE  0x80

// port registers (byte offsets in a port's set)
#define PX_CLB  0x00
#define PX_CLBU 0x04
#define PX_FB   0x08
#define PX_FBU  0x0C
#define PX_IS   0x10
#define PX_IE   0x14
#define PX_CMD  0x18
#define PX_TFD  0x20
#define PX_SIG  0x24
#define PX_SSTS 0x28
#define PX_SERR 0x30
#define PX_CI   0x38

#define CMD_ST  (1u << 0)  // start processing the command list
#define CMD_FRE (1u << 4)  // FIS receive enable
#define CMD_FR  (1u << 14) // FIS receive running
#define CMD_CR  (1u << 15) // command list running

#define TFD_ERR (1u << 0)
#define TFD_DRQ (1u << 3)
#define TFD_BSY (1u << 7)
#define IS_TFES (1u << 30) // task file error

#define SIG_SATA_DISK 0x00000101u
#define SSTS_DET_PRESENT 3u
#define SSTS_IPM_ACTIVE  1u

// ATA commands
#define ATA_READ_DMA_EXT    0x25
#define ATA_WRITE_DMA_EXT   0x35
#define ATA_FLUSH_CACHE_EXT 0xEA
#define ATA_IDENTIFY        0xEC

#define FIS_TYPE_H2D 0x27

// the layout of the per-port page
#define OFF_CMD_LIST  0x000
#define OFF_FIS       0x400
#define OFF_CMD_TABLE 0x500
#define OFF_IDENTIFY  0x600

#define SECTOR_SIZE     512u
#define BOUNCE_SECTORS  (PAGE_SIZE / SECTOR_SIZE)
#define SPIN_LIMIT      100000000u

static volatile uint8_t *abar;
static bool dma_64;
static ahci_disk_t disks[AHCI_MAX_DISKS];
static uint32_t disk_count;

static uint32_t hba_read(uint32_t reg)
{
    return mmio_read32(abar + reg);
}

static void hba_write(uint32_t reg, uint32_t value)
{
    mmio_write32(abar + reg, value);
}

static uint32_t port_read(const ahci_disk_t *disk, uint32_t reg)
{
    return mmio_read32(&disk->port[reg / 4]);
}

static void port_write(const ahci_disk_t *disk, uint32_t reg, uint32_t value)
{
    mmio_write32(&disk->port[reg / 4], value);
}

/**
 * the bounce page of a disk follows its control page.
 */
static uint8_t *bounce(const ahci_disk_t *disk)
{
    return disk->dma_page + PAGE_SIZE;
}

static phys_addr_t bounce_phys(const ahci_disk_t *disk)
{
    return disk->dma_phys + PAGE_SIZE;
}

/**
 * stop the command engine and the FIS receive engine of a port.
 */
static bool port_stop(ahci_disk_t *disk)
{
    port_write(disk, PX_CMD, port_read(disk, PX_CMD) & ~(CMD_ST | CMD_FRE));
    for (uint32_t spin = 0; port_read(disk, PX_CMD) & (CMD_CR | CMD_FR); spin++)
    {
        if (spin > SPIN_LIMIT)
        {
            return false;
        }
    }
    return true;
}

static void port_start(ahci_disk_t *disk)
{
    for (uint32_t spin = 0; port_read(disk, PX_CMD) & CMD_CR; spin++)
    {
        if (spin > SPIN_LIMIT)
        {
            break;
        }
    }
    port_write(disk, PX_CMD, port_read(disk, PX_CMD) | CMD_FRE);
    port_write(disk, PX_CMD, port_read(disk, PX_CMD) | CMD_ST);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    memcpy(p, &value, sizeof(value));
}

/**
 * run one command in slot 0 and wait for it.
 * @param command the ATA command.
 * @param lba the first sector.
 * @param count sectors (0 for a command without data).
 * @param write the data goes to the disk.
 * @param data_phys the physical address of the data (bytes = count * 512).
 * @return false on a task file error or a timeout.
 */
static bool run_command(ahci_disk_t *disk, uint8_t command, uint64_t lba, uint32_t count,
                        bool write, phys_addr_t data_phys, uint32_t bytes)
{
    uint8_t *header = disk->dma_page + OFF_CMD_LIST;
    uint8_t *table = disk->dma_page + OFF_CMD_TABLE;
    phys_addr_t table_phys = disk->dma_phys + OFF_CMD_TABLE;

    // wait until the device is not busy
    for (uint32_t spin = 0; port_read(disk, PX_TFD) & (TFD_BSY | TFD_DRQ); spin++)
    {
        if (spin > SPIN_LIMIT)
        {
            return false;
        }
    }

    // command header: FIS length 5 dwords, write bit, one PRDT entry
    memset(header, 0, 32);
    uint32_t flags = 5u | (write ? (1u << 6) : 0) | ((bytes ? 1u : 0) << 16);
    write_le32(header, flags);
    write_le32(header + 8, (uint32_t)table_phys);
    write_le32(header + 12, (uint32_t)((uint64_t)table_phys >> 32));

    // command table: the register FIS (host to device), then the PRDT
    memset(table, 0, 0x80 + 16);
    table[0] = FIS_TYPE_H2D;
    table[1] = 0x80; // this FIS is a command
    table[2] = command;
    table[4] = (uint8_t)lba;
    table[5] = (uint8_t)(lba >> 8);
    table[6] = (uint8_t)(lba >> 16);
    table[7] = 0x40; // LBA mode
    table[8] = (uint8_t)(lba >> 24);
    table[9] = (uint8_t)(lba >> 32);
    table[10] = (uint8_t)(lba >> 40);
    table[12] = (uint8_t)count;
    table[13] = (uint8_t)(count >> 8);

    if (bytes)
    {
        uint8_t *prd = table + 0x80;
        write_le32(prd, (uint32_t)data_phys);
        write_le32(prd + 4, (uint32_t)((uint64_t)data_phys >> 32));
        write_le32(prd + 12, bytes - 1); // byte count - 1 (interrupt bit clear)
    }

    port_write(disk, PX_IS, 0xFFFFFFFFu); // clear old status
    port_write(disk, PX_CI, 1u);           // issue slot 0

    for (uint32_t spin = 0; port_read(disk, PX_CI) & 1u; spin++)
    {
        if ((port_read(disk, PX_IS) & IS_TFES) || spin > SPIN_LIMIT)
        {
            return false;
        }
    }
    return !(port_read(disk, PX_TFD) & TFD_ERR) && !(port_read(disk, PX_IS) & IS_TFES);
}

static bool ahci_transfer(ahci_disk_t *disk, uint32_t start, uint8_t count, void *buffer,
                          bool write)
{
    if ((uint64_t)start + count > disk->sectors)
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
            memcpy(bounce(disk), data, bytes);
        }
        ok = run_command(disk, write ? ATA_WRITE_DMA_EXT : ATA_READ_DMA_EXT, start, chunk, write,
                         bounce_phys(disk), bytes);
        if (ok && !write)
        {
            memcpy(data, bounce(disk), bytes);
        }
        data += bytes;
        start += chunk;
        count = (uint8_t)(count - chunk);
    }
    arch_irq_restore(flags);
    return ok;
}

static bool ahci_read(block_device_t *device, uint32_t start, uint8_t count, void *dest)
{
    return ahci_transfer(device->context, start, count, dest, false);
}

static bool ahci_write(block_device_t *device, uint32_t start, uint8_t count, const void *src)
{
    return ahci_transfer(device->context, start, count, (void *)src, true);
}

static bool ahci_flush(block_device_t *device)
{
    ahci_disk_t *disk = device->context;
    uint32_t flags = arch_irq_save();
    bool ok = run_command(disk, ATA_FLUSH_CACHE_EXT, 0, 0, false, 0, 0);
    arch_irq_restore(flags);
    return ok;
}

/**
 * allocate the two pages of a port: physically contiguous and below 4 GiB
 * if the HBA cannot use 64-bit addresses.
 */
static bool alloc_dma(ahci_disk_t *disk)
{
    uint8_t *pages = pmm_alloc_contiguous(2);
    if (!pages)
    {
        return false;
    }
    phys_addr_t phys = virt_to_phys(pages);
    if (!dma_64 && ((uint64_t)phys + 2 * PAGE_SIZE) > 0x100000000ull)
    {
        return false;
    }
    memset(pages, 0, 2 * PAGE_SIZE);
    disk->dma_page = pages;
    disk->dma_phys = phys;
    return true;
}

/**
 * set up one port with a SATA disk: stop it, point it at our command list
 * and FIS area, start it, and read IDENTIFY DEVICE.
 */
static bool setup_port(uint32_t number)
{
    ahci_disk_t *disk = &disks[disk_count];
    memset(disk, 0, sizeof(*disk));
    disk->port = (volatile uint32_t *)(abar + PORT_BASE + number * PORT_SIZE);
    disk->port_number = (uint8_t)number;

    uint32_t ssts = port_read(disk, PX_SSTS);
    if ((ssts & 0xF) != SSTS_DET_PRESENT || ((ssts >> 8) & 0xF) != SSTS_IPM_ACTIVE ||
        port_read(disk, PX_SIG) != SIG_SATA_DISK)
    {
        return false; // no disk, or not an ATA disk (ATAPI, port multiplier)
    }

    if (!port_stop(disk) || !alloc_dma(disk))
    {
        return false;
    }

    phys_addr_t list = disk->dma_phys + OFF_CMD_LIST;
    phys_addr_t fis = disk->dma_phys + OFF_FIS;
    port_write(disk, PX_CLB, (uint32_t)list);
    port_write(disk, PX_CLBU, (uint32_t)((uint64_t)list >> 32));
    port_write(disk, PX_FB, (uint32_t)fis);
    port_write(disk, PX_FBU, (uint32_t)((uint64_t)fis >> 32));
    port_write(disk, PX_SERR, 0xFFFFFFFFu);
    port_write(disk, PX_IS, 0xFFFFFFFFu);
    port_write(disk, PX_IE, 0); // polled
    port_start(disk);

    uint8_t *identify = disk->dma_page + OFF_IDENTIFY;
    if (!run_command(disk, ATA_IDENTIFY, 0, 0, false, disk->dma_phys + OFF_IDENTIFY, 512))
    {
        return false;
    }

    // words 100-103: the number of sectors for 48-bit commands
    uint64_t sectors;
    memcpy(&sectors, identify + 200, sizeof(sectors));
    if (sectors == 0)
    {
        return false;
    }

    disk->sectors = sectors;
    disk->block.bytes_per_block = SECTOR_SIZE;
    disk->block.total_blocks = sectors > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)sectors;
    disk->block.context = disk;
    disk->block.read_blocks = ahci_read;
    disk->block.write_blocks = ahci_write;
    disk->block.flush = ahci_flush;

    KLOG_INFO("ahci: port %u: %u MiB", number, (uint32_t)(sectors / 2048));
    disk_count++;
    return true;
}

uint32_t ahci_init(void)
{
    pci_device_t *dev = pci_find_class(PCI_CLASS_STORAGE, PCI_SUBCLASS_SATA, 0x01, 0);
    if (!dev || dev->bars[5].base == 0 || dev->bars[5].is_io)
    {
        return 0;
    }
    if ((dev->bars[5].base >> 32) != 0 && sizeof(phys_addr_t) < 8)
    {
        return 0;
    }

    abar = ioremap_nocache((phys_addr_t)dev->bars[5].base,
                           dev->bars[5].size ? (size_t)dev->bars[5].size : 0x1100);
    if (!abar)
    {
        return 0;
    }
    pci_enable_bus_master(dev);

    hba_write(HBA_GHC, hba_read(HBA_GHC) | GHC_AE);
    dma_64 = (hba_read(HBA_CAP) & CAP_S64A) != 0;

    uint32_t implemented = hba_read(HBA_PI);
    for (uint32_t port = 0; port < 32 && disk_count < AHCI_MAX_DISKS; port++)
    {
        if (implemented & (1u << port))
        {
            setup_port(port);
        }
    }

    KLOG_INFO("ahci: %u disk(s)", disk_count);
    return disk_count;
}

ahci_disk_t *ahci_disk(uint32_t index)
{
    return index < disk_count ? &disks[index] : NULL;
}
