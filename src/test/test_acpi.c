/**
 * @file test_acpi.c
 * @brief the _S5 package in DSDT bytes, in the forms firmware writes it.
 */

#include "ktest.h"

#include "kernel/acpi/acpi.h"

KTEST(acpi_s5_with_byte_prefixes)
{
    // Name (_S5, Package (0x04) {0x05, 0x05, 0, 0}) after other bytes
    static const uint8_t aml[] = {0x10, 0x20, 0x08, '_', 'S', '5', '_', 0x12, 0x0A, 0x04,
                                  0x0A, 0x05, 0x0A, 0x05, 0x00, 0x00};
    uint8_t a = 9;
    uint8_t b = 9;
    KEXPECT(acpi_find_s5(aml, sizeof(aml), &a, &b));
    KEXPECT_EQ(a, 5);
    KEXPECT_EQ(b, 5);
}

KTEST(acpi_s5_with_zero_ops_and_a_root_name)
{
    // Name (\_S5, Package () {Zero, Zero}) as QEMU writes it
    static const uint8_t aml[] = {0x08, '\\', '_', 'S', '5', '_', 0x12, 0x04, 0x02, 0x00, 0x00};
    uint8_t a = 9;
    uint8_t b = 9;
    KEXPECT(acpi_find_s5(aml, sizeof(aml), &a, &b));
    KEXPECT_EQ(a, 0);
    KEXPECT_EQ(b, 0);
}

KTEST(acpi_s5_not_a_name_is_skipped)
{
    // "_S5_" in a method call, not a NameOp: there is no package
    static const uint8_t aml[] = {0x14, '_', 'S', '5', '_', 0x12, 0x04, 0x02, 0x01, 0x01};
    uint8_t a = 0;
    uint8_t b = 0;
    KEXPECT(!acpi_find_s5(aml, sizeof(aml), &a, &b));
}
