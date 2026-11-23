/**
 * the ACPI static tables (ACPI specification, chapter 5.2).
 *
 * the RSDP points to the RSDT (32-bit table addresses) and, from ACPI 2.0,
 * to the XSDT (64-bit table addresses). each table starts with a 36-byte
 * header with a signature, a length and a checksum (all bytes add up to 0).
 * the tables are in reserved or ACPI memory, which is not always in the
 * direct map, so the kernel maps each table with ioremap.
 */

#include "acpi.h"

#include "arch/arch.h"
#include "arch/x86/cpu/io.h"
#include "common/memory.h"
#include "common/string.h"
#include "drivers/pci/pci.h"
#include "kernel/klog/klog.h"
#include "kernel/memory/mmio.h"
#include "kernel/memory/phys.h"

#define SDT_HEADER_SIZE 36
#define TABLE_MAX_SIZE  (256u * 1024u) // a sanity limit

// MADT entry types
#define MADT_LAPIC          0
#define MADT_IOAPIC         1
#define MADT_ISO            2
#define MADT_LAPIC_OVERRIDE 5

#define MADT_PCAT_COMPAT 0x1u
#define LAPIC_ENABLED    0x1u
#define LAPIC_ONLINE_OK  0x2u

#define FADT_RESET_REG_SUP (1u << 10)

static acpi_info_t info;

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

static bool checksum_ok(const uint8_t *data, uint32_t length)
{
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++)
    {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum == 0;
}

/**
 * map a whole table, or NULL if it cannot be mapped or its checksum is bad.
 */
static const uint8_t *map_table(uint64_t phys)
{
    if (phys == 0 || (phys >> 32) != 0)
    {
        return NULL; // above 4 GiB: not on the machines the kernel supports
    }

    const uint8_t *header = ioremap((phys_addr_t)phys, SDT_HEADER_SIZE);
    if (!header)
    {
        return NULL;
    }
    uint32_t length = read32(header + 4);
    if (length < SDT_HEADER_SIZE || length > TABLE_MAX_SIZE)
    {
        return NULL;
    }

    const uint8_t *table = ioremap((phys_addr_t)phys, length);
    if (!table || !checksum_ok(table, length))
    {
        return NULL;
    }
    return table;
}

static void read_madt(const uint8_t *madt)
{
    uint32_t length = read32(madt + 4);
    info.has_madt = true;
    info.lapic_address = read32(madt + 36);
    info.has_8259 = (read32(madt + 40) & MADT_PCAT_COMPAT) != 0;

    for (uint32_t offset = 44; offset + 2 <= length;)
    {
        const uint8_t *entry = madt + offset;
        uint8_t type = entry[0];
        uint8_t size = entry[1];
        if (size < 2 || offset + size > length)
        {
            break;
        }

        switch (type)
        {
        case MADT_LAPIC:
            if (size >= 8 && (read32(entry + 4) & (LAPIC_ENABLED | LAPIC_ONLINE_OK)) &&
                info.cpu_count < ACPI_MAX_CPUS)
            {
                info.cpu_apic_ids[info.cpu_count++] = entry[3];
            }
            break;
        case MADT_IOAPIC:
            if (size >= 12 && info.ioapic_count < ACPI_MAX_IOAPICS)
            {
                acpi_ioapic_t *ioapic = &info.ioapics[info.ioapic_count++];
                ioapic->id = entry[2];
                ioapic->address = read32(entry + 4);
                ioapic->gsi_base = read32(entry + 8);
            }
            break;
        case MADT_ISO:
            if (size >= 10 && entry[2] == 0 && info.iso_count < ACPI_MAX_ISOS)
            {
                // flags bits 0-1: polarity (3 = active low), bits 2-3:
                // trigger mode (3 = level)
                uint16_t flags = (uint16_t)(entry[8] | (entry[9] << 8));
                acpi_iso_t *iso = &info.isos[info.iso_count++];
                iso->source = entry[3];
                iso->gsi = read32(entry + 4);
                iso->active_low = (flags & 0x3u) == 0x3u;
                iso->level_triggered = ((flags >> 2) & 0x3u) == 0x3u;
            }
            break;
        case MADT_LAPIC_OVERRIDE:
            if (size >= 12 && (read64(entry + 4) >> 32) == 0)
            {
                info.lapic_address = (uint32_t)read64(entry + 4);
            }
            break;
        default:
            break;
        }
        offset += size;
    }
}

/**
 * read one small AML number: ZeroOp, OneOp, a BytePrefix value, or a
 * plain byte (some firmware writes it so).
 * @return the bytes it used, 0 if it is not a small number.
 */
static uint32_t aml_small(const uint8_t *p, const uint8_t *end, uint8_t *value)
{
    if (p >= end)
    {
        return 0;
    }
    if (*p == 0x0A && p + 1 < end) // BytePrefix
    {
        *value = p[1];
        return 2;
    }
    if (*p <= 0x07) // ZeroOp is 0, OneOp is 1; the sleep types are 3 bits
    {
        *value = *p;
        return 1;
    }
    return 0;
}

bool acpi_find_s5(const uint8_t *aml, uint32_t length, uint8_t *type_a, uint8_t *type_b)
{
    const uint8_t *end = aml + length;
    for (const uint8_t *p = aml; p + 4 <= end; p++)
    {
        // "_S5_" after a NameOp (0x08), or after NameOp and a root '\'
        if (memcmp(p, "_S5_", 4) != 0 || p == aml ||
            !(p[-1] == 0x08 || (p - aml >= 2 && p[-1] == '\\' && p[-2] == 0x08)))
        {
            continue;
        }
        const uint8_t *q = p + 4;
        if (q >= end || *q != 0x12) // PackageOp
        {
            continue;
        }
        q++;
        if (q >= end)
        {
            return false;
        }
        q += 1 + (*q >> 6); // PkgLength: the top 2 bits count the bytes after it
        q++;                // NumElements
        uint32_t used = aml_small(q, end, type_a);
        if (!used)
        {
            continue;
        }
        q += used;
        if (!aml_small(q, end, type_b))
        {
            *type_b = 0;
        }
        return true;
    }
    return false;
}

static void read_dsdt(uint64_t phys)
{
    const uint8_t *dsdt = map_table(phys);
    if (dsdt)
    {
        info.has_s5 = acpi_find_s5(dsdt + SDT_HEADER_SIZE, read32(dsdt + 4) - SDT_HEADER_SIZE,
                                   &info.s5_type_a, &info.s5_type_b);
    }
}

static void read_fadt(const uint8_t *fadt)
{
    uint32_t length = read32(fadt + 4);
    if (length >= 72)
    {
        info.smi_command = read32(fadt + 48);
        info.acpi_enable = fadt[52];
        info.pm1a_control = (uint16_t)read32(fadt + 64);
        info.pm1b_control = (uint16_t)read32(fadt + 68);
        // the DSDT: the 64-bit X_DSDT (ACPI 2.0) when there is one
        uint64_t dsdt = read32(fadt + 40);
        if (length >= 148 && read64(fadt + 140) != 0)
        {
            dsdt = read64(fadt + 140);
        }
        read_dsdt(dsdt);
    }
    // the reset register (a generic address structure at 116) and the
    // reset value (at 128) are in ACPI 2.0 and later FADTs
    if (length < 129 || !(read32(fadt + 112) & FADT_RESET_REG_SUP))
    {
        return;
    }
    info.has_reset_register = true;
    info.reset_space = fadt[116];
    info.reset_address = read64(fadt + 120);
    info.reset_value = fadt[128];
}

static void read_table(uint64_t phys)
{
    const uint8_t *table = map_table(phys);
    if (!table)
    {
        return;
    }

    if (memcmp(table, "APIC", 4) == 0)
    {
        read_madt(table);
    }
    else if (memcmp(table, "FACP", 4) == 0)
    {
        read_fadt(table);
    }
    else if (memcmp(table, "HPET", 4) == 0 && read32(table + 4) >= 56)
    {
        info.hpet_address = read64(table + 44); // the address of the GAS
    }
    else if (memcmp(table, "MCFG", 4) == 0 && read32(table + 4) >= 52)
    {
        info.mcfg_address = read64(table + 44); // the first allocation
    }
}

/**
 * find "RSD PTR " on a 16-byte boundary in [start, end) of low memory.
 */
static const uint8_t *scan_rsdp(phys_addr_t start, phys_addr_t end)
{
    for (phys_addr_t p = start; p + 20 <= end; p += 16)
    {
        const uint8_t *candidate = phys_to_virt(p);
        if (memcmp(candidate, "RSD PTR ", 8) == 0 && checksum_ok(candidate, 20))
        {
            return candidate;
        }
    }
    return NULL;
}

/**
 * the RSDP: the bootloader's copy, else a scan of the first KiB of the EBDA
 * and of the BIOS ROM area (0xE0000 to 0xFFFFF).
 */
static bool find_rsdp(const boot_info_t *boot, uint8_t *rsdp)
{
    if (boot->rsdp_size >= 20 && checksum_ok(boot->rsdp, 20))
    {
        memcpy(rsdp, boot->rsdp, boot->rsdp_size);
        return true;
    }

    // the BIOS areas exist only on a BIOS boot (not under UEFI)
    if (boot->boot_protocol == BOOT_PROTOCOL_LIMINE)
    {
        return false;
    }

    phys_addr_t ebda = (phys_addr_t)(*(const uint16_t *)phys_to_virt(0x40E)) << 4;
    const uint8_t *found = NULL;
    if (ebda >= 0x80000 && ebda < 0xA0000)
    {
        found = scan_rsdp(ebda, ebda + 1024);
    }
    if (!found)
    {
        found = scan_rsdp(0xE0000, 0x100000);
    }
    if (!found)
    {
        return false;
    }

    uint32_t size = found[15] >= 2 ? 36 : 20;
    memcpy(rsdp, found, size);
    return true;
}

bool acpi_init(const boot_info_t *boot)
{
    uint8_t rsdp[36];
    memset(rsdp, 0, sizeof(rsdp));
    if (!find_rsdp(boot, rsdp))
    {
        KLOG_WARN("acpi: no RSDP");
        return false;
    }

    info.present = true;
    info.revision = rsdp[15];

    // the XSDT if there is one (ACPI 2.0), else the RSDT
    uint64_t xsdt = info.revision >= 2 ? read64(rsdp + 24) : 0;
    const uint8_t *sdt = xsdt ? map_table(xsdt) : NULL;
    uint32_t entry_size = 8;
    if (!sdt)
    {
        sdt = map_table(read32(rsdp + 16));
        entry_size = 4;
    }
    if (!sdt)
    {
        KLOG_WARN("acpi: no RSDT or XSDT");
        return false;
    }

    uint32_t length = read32(sdt + 4);
    for (uint32_t offset = SDT_HEADER_SIZE; offset + entry_size <= length; offset += entry_size)
    {
        read_table(entry_size == 8 ? read64(sdt + offset) : read32(sdt + offset));
    }

    KLOG_INFO("acpi: revision %u, %u cpus, %u io apics, %u overrides%s", info.revision,
              info.cpu_count, info.ioapic_count, info.iso_count,
              info.has_reset_register ? ", reset register" : "");
    if (info.has_s5)
    {
        KLOG_INFO("acpi: power off by S5 (types %u %u)", info.s5_type_a, info.s5_type_b);
    }
    return true;
}

const acpi_info_t *acpi_info(void)
{
    return &info;
}

uint32_t acpi_irq_to_gsi(uint8_t irq, bool *active_low, bool *level)
{
    // ISA interrupts are edge triggered and active high by default
    bool low = false;
    bool lvl = false;
    uint32_t gsi = irq;
    for (uint32_t i = 0; i < info.iso_count; i++)
    {
        if (info.isos[i].source == irq)
        {
            gsi = info.isos[i].gsi;
            low = info.isos[i].active_low;
            lvl = info.isos[i].level_triggered;
            break;
        }
    }
    if (active_low)
    {
        *active_low = low;
    }
    if (level)
    {
        *level = lvl;
    }
    return gsi;
}

/**
 * write the reset value to the FADT reset register.
 */
static void reset_through_fadt(void)
{
    if (!info.has_reset_register)
    {
        return;
    }

    switch (info.reset_space)
    {
    case 0: // system memory
        if ((info.reset_address >> 32) == 0)
        {
            volatile uint8_t *reg = ioremap_nocache((phys_addr_t)info.reset_address, 1);
            if (reg)
            {
                *reg = info.reset_value;
            }
        }
        break;
    case 1: // system I/O
        io_outb((uint16_t)info.reset_address, info.reset_value);
        break;
    case 2: // PCI configuration space of bus 0: device in bits 32-47,
            // function in bits 16-31, register in bits 0-15
    {
        pci_device_t dev;
        memset(&dev, 0, sizeof(dev));
        dev.device = (uint8_t)(info.reset_address >> 32);
        dev.function = (uint8_t)(info.reset_address >> 16);
        uint8_t reg = (uint8_t)info.reset_address;
        uint32_t word = pci_read32(&dev, reg & 0xFCu);
        uint32_t shift = (reg & 3u) * 8;
        word = (word & ~(0xFFu << shift)) | ((uint32_t)info.reset_value << shift);
        pci_write32(&dev, reg & 0xFCu, word);
        break;
    }
    default:
        break;
    }
}

void acpi_power_off(void)
{
    arch_disable_interrupts();
    if (info.has_s5 && info.pm1a_control)
    {
        // the firmware may still own ACPI (SCI_EN clear): ask for it
        if (!(io_inw(info.pm1a_control) & 1u) && info.smi_command && info.acpi_enable)
        {
            io_outb((uint16_t)info.smi_command, info.acpi_enable);
            for (int i = 0; i < 1000000 && !(io_inw(info.pm1a_control) & 1u); i++)
            {
            }
        }
        // SLP_TYP in bits 10 to 12, SLP_EN in bit 13
        io_outw(info.pm1a_control, (uint16_t)((info.s5_type_a & 7u) << 10 | 1u << 13));
        if (info.pm1b_control)
        {
            io_outw(info.pm1b_control, (uint16_t)((info.s5_type_b & 7u) << 10 | 1u << 13));
        }
    }

    // QEMU (newer machines, then older ones and Bochs), VirtualBox
    io_outw(0x604, 0x2000);
    io_outw(0xB004, 0x2000);
    io_outw(0x4004, 0x3400);
    arch_halt_forever();
}

void acpi_reboot(void)
{
    arch_disable_interrupts();
    reset_through_fadt();

    // the keyboard controller can pulse the CPU reset line: wait until its
    // input buffer is empty, then send the command 0xFE
    for (int i = 0; i < 100000 && (io_inb(0x64) & 0x02); i++)
    {
    }
    io_outb(0x64, 0xFE);

    // as a last way, a triple fault: load an empty IDT and cause a fault
    struct __attribute__((packed))
    {
        uint16_t limit;
        uintptr_t base;
    } empty_idt = {0, 0};
    __asm__ volatile("lidt %0\n\tint3" : : "m"(empty_idt));

    arch_halt_forever();
}
