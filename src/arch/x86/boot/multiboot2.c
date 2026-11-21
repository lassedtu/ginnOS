/**
 * read the multiboot 2 boot information (the multiboot 2 specification,
 * section 3.6) into a boot_info_t.
 *
 * the information starts with its total size and a reserved word. tags
 * follow, each aligned to 8 bytes, each with a type and a size. tag type 0
 * is the last tag. the tag fields are read with memcpy, because the kernel
 * must not assume their alignment.
 */

#include "multiboot2.h"

#include "common/memory.h"
#include "kernel/memory/phys.h"

#define TAG_END         0
#define TAG_CMDLINE     1
#define TAG_MODULE      3
#define TAG_MMAP        6
#define TAG_FRAMEBUFFER 8
#define TAG_ACPI_OLD    14
#define TAG_ACPI_NEW    15

#define FB_TYPE_RGB 1

/* the information must be smaller than this (a sanity limit) */
#define INFO_MAX_SIZE (1024u * 1024u)

/* the kernel keeps a copy of the command line */
#define CMDLINE_MAX 256

static char cmdline_copy[CMDLINE_MAX];

static uint32_t read32(const uint8_t *p)
{
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static uint64_t read64(const uint8_t *p)
{
    uint64_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

/**
 * tag 6: keep the usable regions. the PMM uses only usable RAM, so the other
 * regions are not necessary, and real firmware can give more regions than
 * the map holds.
 */
static void read_mmap(const uint8_t *tag, uint32_t size, boot_info_t *boot)
{
    uint32_t entry_size = read32(tag + 8);
    if (entry_size < 24)
    {
        return;
    }

    memory_map_t *map = &boot->memory_map;
    for (uint32_t offset = 16; offset + entry_size <= size; offset += entry_size)
    {
        const uint8_t *entry = tag + offset;
        uint32_t type = read32(entry + 16);
        if (type != 1 || map->count >= 32u)
        {
            continue;
        }

        memory_region_t *region = &map->regions[map->count++];
        region->base = read64(entry);
        region->length = read64(entry + 8);
        region->type = type;
    }
}

/**
 * tag 8: a linear RGB framebuffer below 4 GiB. other types (EGA text,
 * indexed colour) are not used: the console then uses VGA text.
 */
static void read_framebuffer(const uint8_t *tag, uint32_t size, boot_info_t *boot)
{
    if (size < 38)
    {
        return;
    }

    uint64_t addr = read64(tag + 8);
    uint8_t type = tag[29];
    if (type != FB_TYPE_RGB || addr == 0 || (addr >> 32) != 0)
    {
        return;
    }

    boot->framebuffer_addr = (uint32_t)addr;
    boot->framebuffer_pitch = read32(tag + 16);
    boot->framebuffer_width = read32(tag + 20);
    boot->framebuffer_height = read32(tag + 24);
    boot->framebuffer_bpp = tag[28];
    boot->framebuffer_type = 1;

    // the colour fields come after a 16-bit reserved field
    boot->framebuffer_red_shift = tag[32];
    boot->framebuffer_red_size = tag[33];
    boot->framebuffer_green_shift = tag[34];
    boot->framebuffer_green_size = tag[35];
    boot->framebuffer_blue_shift = tag[36];
    boot->framebuffer_blue_size = tag[37];
}

/**
 * tag 1: copy the command line into kernel memory.
 */
static void read_cmdline(const uint8_t *tag, uint32_t size, boot_info_t *boot)
{
    uint32_t length = size > 8 ? size - 8 : 0;
    if (length >= CMDLINE_MAX)
    {
        length = CMDLINE_MAX - 1;
    }
    memcpy(cmdline_copy, tag + 8, length);
    cmdline_copy[length] = '\0';
    boot->cmdline_addr = (uint32_t)virt_to_phys(cmdline_copy);
}

/**
 * tag 3: the first module is the root filesystem image.
 */
static void read_module(const uint8_t *tag, uint32_t size, boot_info_t *boot)
{
    if (size < 16 || boot->initrd_end != 0)
    {
        return;
    }

    uint32_t start = read32(tag + 8);
    uint32_t end = read32(tag + 12);
    if (end > start)
    {
        boot->initrd_start = start;
        boot->initrd_end = end;
    }
}

/**
 * tags 14 and 15: a copy of the RSDP (20 bytes for ACPI 1.0, 36 bytes
 * after). the ACPI 2.0 copy replaces an ACPI 1.0 copy.
 */
static void read_rsdp(const uint8_t *tag, uint32_t size, boot_info_t *boot)
{
    uint32_t length = size > 8 ? size - 8 : 0;
    if (length > sizeof(boot->rsdp))
    {
        length = sizeof(boot->rsdp);
    }
    if (length < 20 || length < boot->rsdp_size)
    {
        return;
    }
    memcpy(boot->rsdp, tag + 8, length);
    boot->rsdp_size = length;
}

bool multiboot2_read(uintptr_t info_phys, boot_info_t *boot)
{
    if (info_phys == 0 || info_phys >= PHYS_MAP_SIZE)
    {
        return false;
    }

    const uint8_t *info = phys_to_virt(info_phys);
    uint32_t total = read32(info);
    if (total < 16 || total > INFO_MAX_SIZE || info_phys + total > PHYS_MAP_SIZE)
    {
        return false;
    }

    boot->boot_protocol = BOOT_PROTOCOL_MULTIBOOT2;

    uint32_t offset = 8;
    while (offset + 8 <= total)
    {
        const uint8_t *tag = info + offset;
        uint32_t type = read32(tag);
        uint32_t size = read32(tag + 4);
        if (type == TAG_END || size < 8 || offset + size > total)
        {
            break;
        }

        switch (type)
        {
        case TAG_CMDLINE:
            read_cmdline(tag, size, boot);
            break;
        case TAG_MODULE:
            read_module(tag, size, boot);
            break;
        case TAG_MMAP:
            read_mmap(tag, size, boot);
            break;
        case TAG_FRAMEBUFFER:
            read_framebuffer(tag, size, boot);
            break;
        case TAG_ACPI_OLD:
        case TAG_ACPI_NEW:
            read_rsdp(tag, size, boot);
            break;
        default:
            break;
        }

        offset += (size + 7u) & ~7u;
    }

    return boot->memory_map.count > 0;
}
