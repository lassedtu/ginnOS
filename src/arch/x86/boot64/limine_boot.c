/**
 * the x86_64 boot: read the limine boot protocol responses into a
 * boot_info_t, then call kernel_main.
 *
 * the kernel puts requests in its image (section .limine_requests, between
 * a start and an end marker). limine finds them before it starts the kernel
 * and writes a pointer to a response into each one it can answer. all
 * pointers in the responses are HHDM addresses (physical + KERNEL_VIRT_BASE).
 * the responses are in bootloader-reclaimable memory, which the PMM does not
 * use, but the kernel copies what it needs anyway.
 *
 * the request layout is from the limine protocol specification
 * (PROTOCOL.md, base revision 6).
 */

#ifdef __x86_64__

#include "common/boot/boot_info.h"
#include "common/memory.h"
#include "common/stdint.h"
#include "kernel/kernel.h"
#include "kernel/memory/phys.h"

#define LIMINE_MAGIC 0xc7b1dd30df4c8b88ul, 0x0a82e883a194f07bul

#define REQUESTS __attribute__((used, section(".limine_requests"), aligned(8)))

// a request: four id words, a revision, and the response pointer
struct limine_request
{
    uint64_t id[4];
    uint64_t revision;
    void *response;
};

struct limine_hhdm_response
{
    uint64_t revision;
    uint64_t offset;
};

struct limine_memmap_entry
{
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct limine_memmap_response
{
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;
};

struct limine_framebuffer
{
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

struct limine_framebuffer_response
{
    uint64_t revision;
    uint64_t framebuffer_count;
    struct limine_framebuffer **framebuffers;
};

struct limine_file
{
    uint64_t revision;
    void *address;
    uint64_t size;
    char *path;
    char *string;
};

struct limine_module_response
{
    uint64_t revision;
    uint64_t module_count;
    struct limine_file **modules;
};

struct limine_rsdp_response
{
    uint64_t revision;
    void *address;
};

struct limine_executable_address_response
{
    uint64_t revision;
    uint64_t physical_base;
    uint64_t virtual_base;
};

struct limine_cmdline_response
{
    uint64_t revision;
    char *cmdline;
};

// memory map types
#define LIMINE_MEMMAP_USABLE                 0
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE       2
#define LIMINE_MEMMAP_ACPI_NVS               3
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_EXECUTABLE_AND_MODULES 6

#define LIMINE_FRAMEBUFFER_RGB 1

__attribute__((used, section(".limine_requests_start"), aligned(8))) static volatile uint64_t
    requests_start[4] = {0xf6b8f4b39de7d1aeul, 0xfab91a6940fcb9cful, 0x785c6ed015d3e316ul,
                         0x181e920a7852b9d9ul};

// base revision 6: the third word becomes 0 if limine supports it
REQUESTS static volatile uint64_t base_revision[3] = {0xf9562b2d5c95a6c8ul,
                                                      0x6a7b384944536bdcul, 6};

REQUESTS static volatile struct limine_request hhdm_request = {
    {LIMINE_MAGIC, 0x48dcf1cb8ad2b852ul, 0x63984e959a98244bul}, 0, NULL};

REQUESTS static volatile struct limine_request memmap_request = {
    {LIMINE_MAGIC, 0x67cf3d9d378a806ful, 0xe304acdfc50c3c62ul}, 0, NULL};

REQUESTS static volatile struct limine_request framebuffer_request = {
    {LIMINE_MAGIC, 0x9d5827dcd881dd75ul, 0xa3148604f6fab11bul}, 0, NULL};

REQUESTS static volatile struct limine_request module_request = {
    {LIMINE_MAGIC, 0x3e7e279702be32aful, 0xca1c4f3bd1280ceeul}, 0, NULL};

REQUESTS static volatile struct limine_request rsdp_request = {
    {LIMINE_MAGIC, 0xc5e77b6b397e7b43ul, 0x27637845accdcf3cul}, 0, NULL};

REQUESTS static volatile struct limine_request executable_address_request = {
    {LIMINE_MAGIC, 0x71ba76863cc55f63ul, 0xb2644a48c516a487ul}, 0, NULL};

REQUESTS static volatile struct limine_request cmdline_request = {
    {LIMINE_MAGIC, 0x4b161536e598651eul, 0xb390ad4a2f1f303aul}, 0, NULL};

__attribute__((used, section(".limine_requests_end"), aligned(8))) static volatile uint64_t
    requests_end[2] = {0xadc0e0531bb10d03ul, 0x9572709f31764c62ul};

phys_addr_t kernel_image_phys;

// the end of the RAM the bootloader reports (paging64.c maps this much)
extern phys_addr_t boot_direct_map_end;

static boot_info_t boot_info;

#define CMDLINE_MAX 256
static char cmdline_copy[CMDLINE_MAX];

/**
 * stop: there is no console yet to tell why. a debugger shows the halt.
 */
static void __attribute__((noreturn)) boot_fail(void)
{
    for (;;)
    {
        __asm__ volatile("cli; hlt");
    }
}

static phys_addr_t hhdm_to_phys(const void *address)
{
    return (phys_addr_t)address - KERNEL_VIRT_BASE;
}

/**
 * keep the usable regions (the PMM uses only those) and find the end of
 * all RAM, for the direct map.
 */
static void read_memmap(const struct limine_memmap_response *map)
{
    for (uint64_t i = 0; i < map->entry_count; i++)
    {
        const struct limine_memmap_entry *entry = map->entries[i];
        uint64_t end = entry->base + entry->length;

        switch (entry->type)
        {
        case LIMINE_MEMMAP_USABLE:
            if (boot_info.memory_map.count < 32u)
            {
                memory_region_t *region =
                    &boot_info.memory_map.regions[boot_info.memory_map.count++];
                region->base = entry->base;
                region->length = entry->length;
                region->type = 1;
            }
            break;
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
        case LIMINE_MEMMAP_ACPI_NVS:
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
            break;
        default:
            continue; // reserved, bad memory, the framebuffer: not RAM
        }

        if (end > boot_direct_map_end)
        {
            boot_direct_map_end = end;
        }
    }
}

static void read_framebuffer(const struct limine_framebuffer_response *response)
{
    if (response->framebuffer_count == 0)
    {
        return;
    }

    const struct limine_framebuffer *fb = response->framebuffers[0];
    phys_addr_t phys = hhdm_to_phys(fb->address);
    if (fb->memory_model != LIMINE_FRAMEBUFFER_RGB || (phys >> 32) != 0)
    {
        return;
    }

    boot_info.framebuffer_addr = (uint32_t)phys;
    boot_info.framebuffer_width = (uint32_t)fb->width;
    boot_info.framebuffer_height = (uint32_t)fb->height;
    boot_info.framebuffer_pitch = (uint32_t)fb->pitch;
    boot_info.framebuffer_bpp = (uint8_t)fb->bpp;
    boot_info.framebuffer_type = 1;
    boot_info.framebuffer_red_size = fb->red_mask_size;
    boot_info.framebuffer_red_shift = fb->red_mask_shift;
    boot_info.framebuffer_green_size = fb->green_mask_size;
    boot_info.framebuffer_green_shift = fb->green_mask_shift;
    boot_info.framebuffer_blue_size = fb->blue_mask_size;
    boot_info.framebuffer_blue_shift = fb->blue_mask_shift;
}

/**
 * the first module is the root filesystem image.
 */
static void read_module(const struct limine_module_response *response)
{
    if (response->module_count == 0)
    {
        return;
    }

    const struct limine_file *file = response->modules[0];
    phys_addr_t start = hhdm_to_phys(file->address);
    if ((start >> 32) != 0 || file->size == 0 || file->size > 0xFFFFFFFFul - start)
    {
        return;
    }
    boot_info.initrd_start = (uint32_t)start;
    boot_info.initrd_end = (uint32_t)(start + file->size);
}

/**
 * copy the RSDP: 20 bytes for ACPI 1.0 (revision 0), 36 bytes after.
 */
static void read_rsdp(const struct limine_rsdp_response *response)
{
    const uint8_t *rsdp = response->address;
    if (!rsdp)
    {
        return;
    }
    uint32_t size = rsdp[15] >= 2 ? 36u : 20u;
    memcpy(boot_info.rsdp, rsdp, size);
    boot_info.rsdp_size = size;
}

static void read_cmdline(const struct limine_cmdline_response *response)
{
    if (!response->cmdline)
    {
        return;
    }
    uint32_t i = 0;
    while (i < CMDLINE_MAX - 1 && response->cmdline[i] != '\0')
    {
        cmdline_copy[i] = response->cmdline[i];
        i++;
    }
    cmdline_copy[i] = '\0';
    boot_info.cmdline_addr = (uint32_t)virt_to_phys(cmdline_copy);
}

/**
 * limine enables the local APIC (base revision 5 and later) and masks its
 * LINT0 input, so the interrupts of the 8259 PIC do not arrive. with the
 * APIC off (IA32_APIC_BASE bit 11), the PIC is connected to the CPU again.
 * the kernel uses the PIC until it has an APIC driver.
 */
static void disable_local_apic(void)
{
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1Bu));
    low &= ~(1u << 11);
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0x1Bu));
}

void cstart(void)
{
    (void)requests_start;
    (void)requests_end;

    const struct limine_hhdm_response *hhdm = hhdm_request.response;
    const struct limine_executable_address_response *address =
        executable_address_request.response;
    const struct limine_memmap_response *memmap = memmap_request.response;

    // the kernel needs base revision 6, the direct map where it expects it,
    // its physical address, and the memory map
    if (base_revision[2] != 0 || !hhdm || hhdm->offset != KERNEL_VIRT_BASE || !address ||
        address->virtual_base != KERNEL_IMAGE_BASE || !memmap)
    {
        boot_fail();
    }

    kernel_image_phys = address->physical_base;
    boot_info.boot_protocol = BOOT_PROTOCOL_LIMINE;
    read_memmap(memmap);

    if (framebuffer_request.response)
    {
        read_framebuffer(framebuffer_request.response);
    }
    if (module_request.response)
    {
        read_module(module_request.response);
    }
    if (rsdp_request.response)
    {
        read_rsdp(rsdp_request.response);
    }
    if (cmdline_request.response)
    {
        read_cmdline(cmdline_request.response);
    }

    disable_local_apic();
    kernel_main(&boot_info);
}

#endif // __x86_64__
