#pragma once

/**
 * @file hid.h
 * @brief a small HID report descriptor parser for pointing devices.
 *
 * a USB mouse in the boot protocol sends a fixed report (buttons, x, y);
 * other pointing devices (QEMU's tablet, graphics tablets, touch screens
 * in mouse mode) describe their report in a report descriptor. this
 * parser finds the buttons, x, y and the wheel in it: their bit offset,
 * size and range, and whether x and y are absolute.
 */

#include "common/stdint.h"

typedef struct
{
    bool present;
    uint16_t offset; // bit offset in the report (after the report id byte)
    uint8_t size;    // bits
    int32_t min;     // the logical range
    int32_t max;
} hid_field_t;

typedef struct
{
    uint8_t report_id;  // the report these fields are in (0: no ids)
    bool absolute;      // x and y are positions, not changes
    hid_field_t x;
    hid_field_t y;
    hid_field_t wheel;
    uint16_t buttons_offset;
    uint8_t buttons;    // how many (one bit each)
} hid_pointer_t;

/**
 * find the pointer fields in a report descriptor.
 * @return true if it has an x and a y.
 */
bool hid_parse_pointer(const uint8_t *descriptor, uint32_t length, hid_pointer_t *out);

/**
 * read a field from a report (the report id byte already skipped).
 * @param length the bytes in the report.
 * @return the value, sign-extended when the field's range is signed.
 */
int32_t hid_field_value(const hid_field_t *field, const uint8_t *report, uint32_t length);
