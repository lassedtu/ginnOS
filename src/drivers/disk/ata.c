#include "ata.h"
#include "arch/x86/cpu/io.h"

/*
 * register offsets from the I/O base port.
 * applied as: device->io_base + ATA_REG_*.
 */
#define ATA_REG_DATA 0u         // data port (read/write 16-bit words)
#define ATA_REG_ERROR 1u        // error register (read) / features (write)
#define ATA_REG_SECTOR_COUNT 2u // sector count
#define ATA_REG_LBA_LOW 3u      // LBA bits  7:0
#define ATA_REG_LBA_MID 4u      // LBA bits 15:8
#define ATA_REG_LBA_HIGH 5u     // LBA bits 23:16
#define ATA_REG_DRIVE_HEAD 6u   // drive/head select + LBA bits 27:24 (LBA28)
#define ATA_REG_STATUS 7u       // status (read) / command (write)

/* ATA status register bits */
#define ATA_STATUS_ERR 0x01u // error occurred
#define ATA_STATUS_DF 0x20u  // device fault
#define ATA_STATUS_DRQ 0x08u // data request, drive is ready to transfer
#define ATA_STATUS_BSY 0x80u // busy, do not access other registers

/* ATA commands */
#define ATA_COMMAND_READ_SECTORS 0x20u      // LBA28 read
#define ATA_COMMAND_WRITE_SECTORS 0x30u     // LBA28 write
#define ATA_COMMAND_READ_SECTORS_EXT 0x24u  // LBA48 read
#define ATA_COMMAND_WRITE_SECTORS_EXT 0x34u // LBA48 write
#define ATA_COMMAND_CACHE_FLUSH 0xE7u       // flush write cache
#define ATA_COMMAND_CACHE_FLUSH_EXT 0xEAu   // flush write cache (LBA48 devices)
#define ATA_COMMAND_IDENTIFY 0xECu          // identify device

/* LBA28 ceiling, 28 usable address bits */
#define ATA_LBA28_MAX 0x0FFFFFFFu

/* I/O port constants for each channel */
#define ATA_PRIMARY_IO_BASE 0x1F0u
#define ATA_PRIMARY_CTRL_BASE 0x3F6u
#define ATA_SECONDARY_IO_BASE 0x170u
#define ATA_SECONDARY_CTRL_BASE 0x376u

/* drive-select byte for LBA mode: bit6=LBA, bit5=1 (obsolete), bit7=1 (obsolete) */
#define ATA_DRIVE_SELECT_MASTER 0xE0u // 1110 0000
#define ATA_DRIVE_SELECT_SLAVE 0xF0u  // 1111 0000

/*
 * device control register (written to control_base).
 * nIEN (bit 1): when set, disables the drive's IRQ line so the controller
 * will not assert IRQ 14/15 after a command completes. safe for pure PIO
 * use, all status polling goes through the status register directly.
 */
#define ATA_DCR_NIEN 0x02u // disable interrupts from this device

/**
 * wait 400ns by reading the control port four times.
 */
static void ata_400ns_delay(const ATA_DEVICE *dev)
{
    io_inb(dev->control_base);
    io_inb(dev->control_base);
    io_inb(dev->control_base);
    io_inb(dev->control_base);
}

/**
 * poll until BSY=0, or until timeout. Returns false on timeout.
 * @param dev ATA device to poll.
 * @return true if the device is not busy, false on timeout.
 */
static bool ata_wait_not_busy(const ATA_DEVICE *dev)
{
    uint32_t spin;
    for (spin = 0; spin < 200000u; spin++)
    {
        if ((io_inb((uint16_t)(dev->io_base + ATA_REG_STATUS)) & ATA_STATUS_BSY) == 0)
            return true;
    }
    return false;
}

/**
 * poll until BSY=0 and DRQ=1, or until timeout. Returns false on timeout or error.
 * @param dev ATA device to poll.
 * @return true if the device is ready to transfer data, false on timeout or error.
 */
static bool ata_wait_data_request(const ATA_DEVICE *dev)
{
    uint32_t spin;
    for (spin = 0; spin < 200000u; spin++)
    {
        uint8_t status = io_inb((uint16_t)(dev->io_base + ATA_REG_STATUS));
        if (status & ATA_STATUS_ERR)
            return false;
        if (status & ATA_STATUS_DF)
            return false;
        if (((status & ATA_STATUS_BSY) == 0) && (status & ATA_STATUS_DRQ))
            return true;
    }
    return false;
}

/**
 * read sectors from an ATA device using LBA28 addressing.
 * @param dev ATA device to read from.
 * @param lba 28-bit logical block address to start reading from.
 * @param sector_count number of sectors to read (1-255).
 * @param dest destination buffer to store the read data (must be large enough).
 * @return true on success, false on error or timeout.
 */
static bool ata_read_lba28(ATA_DEVICE *dev, uint32_t lba, uint8_t sector_count, void *dest)
{
    uint8_t *out;
    uint32_t i;

    if (!dest || sector_count == 0 || lba > ATA_LBA28_MAX)
        return false;

    if (!ata_wait_not_busy(dev))
        return false;

    io_outb((uint16_t)(dev->io_base + ATA_REG_DRIVE_HEAD),
            (uint8_t)(dev->drive_select | ((lba >> 24u) & 0x0Fu)));
    ata_400ns_delay(dev);

    io_outb((uint16_t)(dev->io_base + ATA_REG_SECTOR_COUNT), sector_count);
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_LOW), (uint8_t)(lba & 0xFFu));
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_MID), (uint8_t)((lba >> 8u) & 0xFFu));
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_HIGH), (uint8_t)((lba >> 16u) & 0xFFu));
    io_outb((uint16_t)(dev->io_base + ATA_REG_STATUS), ATA_COMMAND_READ_SECTORS);

    out = (uint8_t *)dest;
    for (i = 0; i < sector_count; i++)
    {
        if (!ata_wait_data_request(dev))
            return false;
        io_insw((uint16_t)(dev->io_base + ATA_REG_DATA), out, 256u);
        out += 512u;
    }
    return true;
}

/**
 * write sectors to an ATA device using LBA28 addressing.
 * @param dev ATA device to write to.
 * @param lba 28-bit logical block address to start writing to.
 * @param sector_count number of sectors to write (1-255).
 * @param src source buffer containing the data to write (must be large enough).
 * @return true on success, false on error or timeout.
 */
static bool ata_write_lba28(ATA_DEVICE *dev, uint32_t lba, uint8_t sector_count, const void *src)
{
    const uint8_t *in;
    uint32_t i;

    if (!src || sector_count == 0 || lba > ATA_LBA28_MAX)
        return false;

    if (!ata_wait_not_busy(dev))
        return false;

    io_outb((uint16_t)(dev->io_base + ATA_REG_DRIVE_HEAD),
            (uint8_t)(dev->drive_select | ((lba >> 24u) & 0x0Fu)));
    ata_400ns_delay(dev);

    io_outb((uint16_t)(dev->io_base + ATA_REG_SECTOR_COUNT), sector_count);
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_LOW), (uint8_t)(lba & 0xFFu));
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_MID), (uint8_t)((lba >> 8u) & 0xFFu));
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_HIGH), (uint8_t)((lba >> 16u) & 0xFFu));
    io_outb((uint16_t)(dev->io_base + ATA_REG_STATUS), ATA_COMMAND_WRITE_SECTORS);

    in = (const uint8_t *)src;
    for (i = 0; i < sector_count; i++)
    {
        if (!ata_wait_data_request(dev))
            return false;
        io_outsw((uint16_t)(dev->io_base + ATA_REG_DATA), in, 256u);
        in += 512u;
    }

    if (!ata_wait_not_busy(dev))
        return false;

    io_outb((uint16_t)(dev->io_base + ATA_REG_STATUS), ATA_COMMAND_CACHE_FLUSH);
    return ata_wait_not_busy(dev);
}

/**
 * read sectors from an ATA device using LBA48 addressing.
 * @param dev ATA device to read from.
 * @param lba 48-bit logical block address to start reading from.
 * @param sector_count number of sectors to read (1-255).
 * @param dest destination buffer to store the read data (must be large enough).
 * @return true on success, false on error or timeout.
 */
static bool ata_read_lba48(ATA_DEVICE *dev, uint64_t lba, uint8_t sector_count, void *dest)
{
    uint8_t *out;
    uint32_t i;

    if (!dest || sector_count == 0)
        return false;

    if (!ata_wait_not_busy(dev))
        return false;

    /* Drive select: LBA mode, drive bit only, no LBA bits in this register */
    io_outb((uint16_t)(dev->io_base + ATA_REG_DRIVE_HEAD),
            (uint8_t)(dev->drive_select & 0xF0u));
    ata_400ns_delay(dev);

    /* High bytes first */
    io_outb((uint16_t)(dev->io_base + ATA_REG_SECTOR_COUNT), 0);                           /* count high  */
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_LOW), (uint8_t)((lba >> 24u) & 0xFFu));  /* LBA 31:24   */
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_MID), (uint8_t)((lba >> 32u) & 0xFFu));  /* LBA 39:32   */
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_HIGH), (uint8_t)((lba >> 40u) & 0xFFu)); /* LBA 47:40   */

    /* Low bytes */
    io_outb((uint16_t)(dev->io_base + ATA_REG_SECTOR_COUNT), sector_count);                /* count low   */
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_LOW), (uint8_t)(lba & 0xFFu));           /* LBA  7:0    */
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_MID), (uint8_t)((lba >> 8u) & 0xFFu));   /* LBA 15:8    */
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_HIGH), (uint8_t)((lba >> 16u) & 0xFFu)); /* LBA 23:16   */

    io_outb((uint16_t)(dev->io_base + ATA_REG_STATUS), ATA_COMMAND_READ_SECTORS_EXT);

    out = (uint8_t *)dest;
    for (i = 0; i < sector_count; i++)
    {
        if (!ata_wait_data_request(dev))
            return false;
        io_insw((uint16_t)(dev->io_base + ATA_REG_DATA), out, 256u);
        out += 512u;
    }
    return true;
}

/**
 * write sectors to an ATA device using LBA48 addressing.
 * @param dev ATA device to write to.
 * @param lba 48-bit logical block address to start writing to.
 * @param sector_count number of sectors to write (1-255).
 * @param src source buffer containing the data to write (must be large enough).
 * @return true on success, false on error or timeout.
 */
static bool ata_write_lba48(ATA_DEVICE *dev, uint64_t lba, uint8_t sector_count, const void *src)
{
    const uint8_t *in;
    uint32_t i;

    if (!src || sector_count == 0)
        return false;

    if (!ata_wait_not_busy(dev))
        return false;

    io_outb((uint16_t)(dev->io_base + ATA_REG_DRIVE_HEAD),
            (uint8_t)(dev->drive_select & 0xF0u));
    ata_400ns_delay(dev);

    /* High bytes first */
    io_outb((uint16_t)(dev->io_base + ATA_REG_SECTOR_COUNT), 0);
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_LOW), (uint8_t)((lba >> 24u) & 0xFFu));
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_MID), (uint8_t)((lba >> 32u) & 0xFFu));
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_HIGH), (uint8_t)((lba >> 40u) & 0xFFu));

    /* Low bytes */
    io_outb((uint16_t)(dev->io_base + ATA_REG_SECTOR_COUNT), sector_count);
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_LOW), (uint8_t)(lba & 0xFFu));
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_MID), (uint8_t)((lba >> 8u) & 0xFFu));
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_HIGH), (uint8_t)((lba >> 16u) & 0xFFu));

    io_outb((uint16_t)(dev->io_base + ATA_REG_STATUS), ATA_COMMAND_WRITE_SECTORS_EXT);

    in = (const uint8_t *)src;
    for (i = 0; i < sector_count; i++)
    {
        if (!ata_wait_data_request(dev))
            return false;
        io_outsw((uint16_t)(dev->io_base + ATA_REG_DATA), in, 256u);
        in += 512u;
    }

    if (!ata_wait_not_busy(dev))
        return false;

    io_outb((uint16_t)(dev->io_base + ATA_REG_STATUS), ATA_COMMAND_CACHE_FLUSH_EXT);
    return ata_wait_not_busy(dev);
}

/**
 * read blocks from an ATA device using the block device interface.
 * automatically chooses LBA28 or LBA48 based on the starting block number.
 * @param device block device interface pointer (must point to an ATA_DEVICE).
 * @param startBlock starting block number to read from.
 * @param blockCount number of blocks to read.
 * @param dest destination buffer to store the read data (must be large enough).
 * @return true on success, false on error or timeout.
 */
static bool ata_block_read(BLOCK_DEVICE *device, uint32_t startBlock, uint8_t blockCount, void *dest)
{
    ATA_DEVICE *dev = (ATA_DEVICE *)device->context;

    if (startBlock > ATA_LBA28_MAX)
        return ata_read_lba48(dev, (uint64_t)startBlock, blockCount, dest);

    return ata_read_lba28(dev, startBlock, blockCount, dest);
}

/**
 * write blocks to an ATA device using the block device interface.
 * automatically chooses LBA28 or LBA48 based on the starting block number.
 * @param device block device interface pointer (must point to an ATA_DEVICE).
 * @param startBlock starting block number to write to.
 * @param blockCount number of blocks to write.
 * @param src source buffer containing the data to write (must be large enough).
 * @return true on success, false on error or timeout.
 */
static bool ata_block_write(BLOCK_DEVICE *device, uint32_t startBlock, uint8_t blockCount, const void *src)
{
    ATA_DEVICE *dev = (ATA_DEVICE *)device->context;

    if (startBlock > ATA_LBA28_MAX)
        return ata_write_lba48(dev, (uint64_t)startBlock, blockCount, src);

    return ata_write_lba28(dev, startBlock, blockCount, src);
}

/**
 * perform IDENTIFY DEVICE on an ATA device to confirm presence and capabilities.
 * populates the sector_count field of the ATA_DEVICE structure.
 * @param dev ATA device to identify.
 * @return true if the device is present and not ATAPI, false otherwise.
 */
static bool ata_identify(ATA_DEVICE *dev)
{
    uint8_t status;
    uint16_t id[256];
    uint64_t lba48_sectors;

    io_outb((uint16_t)(dev->io_base + ATA_REG_DRIVE_HEAD), dev->drive_select);
    ata_400ns_delay(dev);

    /* zero sector-count and LBA registers before IDENTIFY (required by spec) */
    io_outb((uint16_t)(dev->io_base + ATA_REG_SECTOR_COUNT), 0);
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_LOW), 0);
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_MID), 0);
    io_outb((uint16_t)(dev->io_base + ATA_REG_LBA_HIGH), 0);

    io_outb((uint16_t)(dev->io_base + ATA_REG_STATUS), ATA_COMMAND_IDENTIFY);

    /* status == 0x00 immediately means no drive on this bus */
    status = io_inb((uint16_t)(dev->io_base + ATA_REG_STATUS));
    if (status == 0x00u)
        return false;

    if (!ata_wait_not_busy(dev))
        return false;

    /* ATAPI devices set LBA_MID/LBA_HIGH to 0x14/0xEB, reject them */
    if (io_inb((uint16_t)(dev->io_base + ATA_REG_LBA_MID)) != 0 ||
        io_inb((uint16_t)(dev->io_base + ATA_REG_LBA_HIGH)) != 0)
        return false;

    if (!ata_wait_data_request(dev))
        return false;

    /* read the full 256-word identify buffer */
    io_insw((uint16_t)(dev->io_base + ATA_REG_DATA), id, 256u);

    /*
     * extract LBA48 sector count from words 100–103 (little-endian pairs).
     * a device that doesn't support LBA48 will have these as zero; fall back
     * to the LBA28 count in words 60–61 in that case.
     */
    lba48_sectors = (uint64_t)id[100] | ((uint64_t)id[101] << 16u) | ((uint64_t)id[102] << 32u) | ((uint64_t)id[103] << 48u);

    if (lba48_sectors != 0)
    {
        dev->sector_count = lba48_sectors;
    }
    else
    {
        dev->sector_count = (uint32_t)id[60] | ((uint32_t)id[61] << 16u);
    }

    return true;
}

/**
 * initialize an ATA device on the specified channel and drive position.
 * issues IDENTIFY DEVICE to confirm the drive is present and is not ATAPI.
 * populates all port fields and wires up the block device interface.
 * @param device   ATA device object to initialize.
 * @param channel  ATA_CHANNEL_PRIMARY or ATA_CHANNEL_SECONDARY.
 * @param drive    ATA_DRIVE_MASTER or ATA_DRIVE_SLAVE.
 * @return true on success, false if no drive responds or device is ATAPI.
 */
bool ATA_Initialize(ATA_DEVICE *device, ATA_CHANNEL channel, ATA_DRIVE drive)
{
    if (!device)
        return false;

    /* derive port addresses from channel */
    if (channel == ATA_CHANNEL_PRIMARY)
    {
        device->io_base = ATA_PRIMARY_IO_BASE;
        device->control_base = ATA_PRIMARY_CTRL_BASE;
    }
    else
    {
        device->io_base = ATA_SECONDARY_IO_BASE;
        device->control_base = ATA_SECONDARY_CTRL_BASE;
    }

    /* derive drive-select byte from drive position */
    device->drive_select = (drive == ATA_DRIVE_MASTER)
                               ? ATA_DRIVE_SELECT_MASTER
                               : ATA_DRIVE_SELECT_SLAVE;

    device->sector_count = 0;

    // disable IRQ generation on this channel before issuing any command.
    // the driver is PIO-only, all completion detection is done via polling
    // the status register, so the ATA interrupt line (IRQ 14 on primary,
    // IRQ 15 on secondary) is never needed and would fire unhandled otherwise.
    io_outb(device->control_base, ATA_DCR_NIEN);

    if (!ata_identify(device))
        return false;

    device->block.bytes_per_block = 512;
    device->block.context = device;
    device->block.read_blocks = ata_block_read;
    device->block.write_blocks = ata_block_write;
    return true;
}
