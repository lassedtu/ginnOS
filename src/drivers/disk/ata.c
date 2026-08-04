#include "ata.h"

#define ATA_IO_BASE_PRIMARY 0x1F0u
#define ATA_IO_DATA (ATA_IO_BASE_PRIMARY + 0u)
#define ATA_IO_SECTOR_COUNT (ATA_IO_BASE_PRIMARY + 2u)
#define ATA_IO_LBA_LOW (ATA_IO_BASE_PRIMARY + 3u)
#define ATA_IO_LBA_MID (ATA_IO_BASE_PRIMARY + 4u)
#define ATA_IO_LBA_HIGH (ATA_IO_BASE_PRIMARY + 5u)
#define ATA_IO_DRIVE_HEAD (ATA_IO_BASE_PRIMARY + 6u)
#define ATA_IO_STATUS_COMMAND (ATA_IO_BASE_PRIMARY + 7u)

#define ATA_IO_CONTROL_BASE 0x3F6u

#define ATA_STATUS_ERR 0x01u
#define ATA_STATUS_DF 0x20u
#define ATA_STATUS_DRQ 0x08u
#define ATA_STATUS_BSY 0x80u

#define ATA_COMMAND_READ_SECTORS 0x20u

static inline void io_outb(uint16_t port, uint8_t value)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t io_inb(uint16_t port)
{
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_insw(uint16_t port, void *buffer, uint32_t word_count)
{
    __asm__ __volatile__("cld; rep insw" : "+D"(buffer), "+c"(word_count) : "d"(port) : "memory");
}

static void ata_400ns_delay(void)
{
    io_inb(ATA_IO_CONTROL_BASE);
    io_inb(ATA_IO_CONTROL_BASE);
    io_inb(ATA_IO_CONTROL_BASE);
    io_inb(ATA_IO_CONTROL_BASE);
}

static bool ata_wait_not_busy(void)
{
    uint32_t spin;

    for (spin = 0; spin < 200000u; spin++)
    {
        uint8_t status = io_inb(ATA_IO_STATUS_COMMAND);
        if ((status & ATA_STATUS_BSY) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool ata_wait_data_request(void)
{
    uint32_t spin;

    for (spin = 0; spin < 200000u; spin++)
    {
        uint8_t status = io_inb(ATA_IO_STATUS_COMMAND);

        if (status & ATA_STATUS_ERR)
        {
            return false;
        }

        if (status & ATA_STATUS_DF)
        {
            return false;
        }

        if (((status & ATA_STATUS_BSY) == 0) && (status & ATA_STATUS_DRQ))
        {
            return true;
        }
    }

    return false;
}

static bool ata_read_lba28(uint32_t lba, uint8_t sector_count, void *dest)
{
    uint8_t *out;
    uint32_t sector_index;

    if (!dest || sector_count == 0)
    {
        return false;
    }

    if (lba > 0x0FFFFFFFu)
    {
        return false;
    }

    if (!ata_wait_not_busy())
    {
        return false;
    }

    io_outb(ATA_IO_DRIVE_HEAD, (uint8_t)(0xE0u | ((lba >> 24u) & 0x0Fu)));
    ata_400ns_delay();

    io_outb(ATA_IO_SECTOR_COUNT, sector_count);
    io_outb(ATA_IO_LBA_LOW, (uint8_t)(lba & 0xFFu));
    io_outb(ATA_IO_LBA_MID, (uint8_t)((lba >> 8u) & 0xFFu));
    io_outb(ATA_IO_LBA_HIGH, (uint8_t)((lba >> 16u) & 0xFFu));
    io_outb(ATA_IO_STATUS_COMMAND, ATA_COMMAND_READ_SECTORS);

    out = (uint8_t *)dest;
    for (sector_index = 0; sector_index < sector_count; sector_index++)
    {
        if (!ata_wait_data_request())
        {
            return false;
        }

        io_insw(ATA_IO_DATA, out, 256u);
        out += 512u;
    }

    return true;
}

static bool ata_block_read(BLOCK_DEVICE *device, uint32_t startBlock, uint8_t blockCount, void *dest)
{
    (void)device;
    return ata_read_lba28(startBlock, blockCount, dest);
}

bool ATA_Initialize(ATA_DEVICE *device)
{
    if (!device)
    {
        return false;
    }

    device->block.bytes_per_block = 512;
    device->block.context = device;
    device->block.read_blocks = ata_block_read;
    return true;
}
