#pragma once

/**
 * @file acpi.h
 * @brief the ACPI tables: RSDP, RSDT/XSDT, MADT, FADT, HPET and MCFG.
 *
 * the kernel reads the static tables only. it has no AML interpreter, so it
 * cannot use the DSDT: no power-off (that needs the _S5 object), no device
 * enumeration through ACPI. reboot uses the FADT reset register.
 */

#include "common/boot/boot_info.h"
#include "common/stdint.h"

#define ACPI_MAX_CPUS    16
#define ACPI_MAX_IOAPICS 4
#define ACPI_MAX_ISOS    16

/** one I/O APIC from the MADT. */
typedef struct
{
    uint8_t id;
    uint32_t address;  // physical address of its registers
    uint32_t gsi_base; // the first global system interrupt it handles
} acpi_ioapic_t;

/**
 * an interrupt source override from the MADT: ISA IRQ source is global
 * system interrupt gsi, with the given polarity and trigger mode.
 */
typedef struct
{
    uint8_t source;
    uint32_t gsi;
    bool active_low;
    bool level_triggered;
} acpi_iso_t;

/** what the kernel takes from the ACPI tables. */
typedef struct
{
    bool present;    // an RSDP with a valid checksum was found
    uint8_t revision; // 0 for ACPI 1.0, 2 or more for XSDT tables

    // MADT
    bool has_madt;
    uint32_t lapic_address; // physical address of the local APIC
    bool has_8259;          // PCAT_COMPAT: there are 8259 PICs too
    uint32_t cpu_count;
    uint8_t cpu_apic_ids[ACPI_MAX_CPUS];
    uint32_t ioapic_count;
    acpi_ioapic_t ioapics[ACPI_MAX_IOAPICS];
    uint32_t iso_count;
    acpi_iso_t isos[ACPI_MAX_ISOS];

    // FADT
    bool has_reset_register;
    uint8_t reset_space; // 0: memory, 1: I/O port, 2: PCI configuration
    uint64_t reset_address;
    uint8_t reset_value;

    // HPET and MCFG
    uint64_t hpet_address; // 0: none
    uint64_t mcfg_address; // the PCI express configuration space, 0: none
} acpi_info_t;

/**
 * find and read the tables. the RSDP comes from the bootloader if it gave
 * one, else from a scan of the BIOS areas. call after mmio_init.
 * @param boot the boot information (rsdp, rsdp_size).
 * @return true if the tables were found.
 */
bool acpi_init(const boot_info_t *boot);

/**
 * what acpi_init found (present is false if it found nothing).
 */
const acpi_info_t *acpi_info(void);

/**
 * the global system interrupt of a legacy ISA IRQ, after the MADT
 * overrides (for example IRQ 0 is often GSI 2).
 * @param irq an ISA IRQ, 0 to 15.
 * @param active_low receives the polarity (may be NULL).
 * @param level receives the trigger mode (may be NULL).
 */
uint32_t acpi_irq_to_gsi(uint8_t irq, bool *active_low, bool *level);

/**
 * restart the machine: the FADT reset register, then the keyboard
 * controller, then a triple fault. does not return.
 */
void acpi_reboot(void) __attribute__((noreturn));
