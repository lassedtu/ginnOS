/**
 * the HID report descriptor parser (USB Device Class Definition for HID
 * 1.11, section 6.2.2).
 *
 * a descriptor is a list of items: a prefix byte (the size in bits 0 to
 * 1, the type in bits 2 to 3, the tag in bits 4 to 7) and 0, 1, 2 or 4
 * data bytes. global items (the usage page, the logical range, the report
 * size, count and id) stay set until changed; local items (the usages)
 * belong to the next main item. an Input main item adds report count
 * fields of report size bits each to the report.
 *
 * not supported: push and pop of the global state, and long items (they
 * are skipped). the pointer fields must all be in one report.
 */

#include "hid.h"

#include "common/memory.h"

#define TYPE_MAIN   0
#define TYPE_GLOBAL 1
#define TYPE_LOCAL  2

#define MAIN_INPUT      0x8
#define GLOBAL_PAGE     0x0
#define GLOBAL_LOG_MIN  0x1
#define GLOBAL_LOG_MAX  0x2
#define GLOBAL_SIZE     0x7
#define GLOBAL_ID       0x8
#define GLOBAL_COUNT    0x9
#define LOCAL_USAGE     0x0
#define LOCAL_USAGE_MIN 0x1
#define LOCAL_USAGE_MAX 0x2

#define INPUT_CONSTANT  0x01
#define INPUT_RELATIVE  0x04

#define PAGE_DESKTOP 0x01
#define PAGE_BUTTON  0x09
#define USAGE_X      0x30
#define USAGE_Y      0x31
#define USAGE_WHEEL  0x38

#define MAX_USAGES 16

static uint32_t item_unsigned(const uint8_t *data, uint32_t size)
{
    uint32_t value = 0;
    for (uint32_t i = 0; i < size; i++)
    {
        value |= (uint32_t)data[i] << (8 * i);
    }
    return value;
}

static int32_t item_signed(const uint8_t *data, uint32_t size)
{
    uint32_t value = item_unsigned(data, size);
    if (size > 0 && size < 4 && (value & (1u << (size * 8 - 1))))
    {
        value |= ~0u << (size * 8); // sign-extend
    }
    return (int32_t)value;
}

bool hid_parse_pointer(const uint8_t *descriptor, uint32_t length, hid_pointer_t *out)
{
    memset(out, 0, sizeof(*out));

    uint32_t page = 0;
    int32_t log_min = 0;
    int32_t log_max = 0;
    uint32_t report_size = 0;
    uint32_t report_count = 0;
    uint32_t report_id = 0;
    uint32_t offset = 0;          // bits so far in the current report
    uint32_t usages[MAX_USAGES];  // (page << 16) | usage
    uint32_t usage_count = 0;
    uint32_t usage_min = 0;
    uint32_t usage_max = 0;
    bool range = false;
    bool found_id = false;

    for (uint32_t pos = 0; pos < length;)
    {
        uint8_t prefix = descriptor[pos];
        if (prefix == 0xFE) // a long item: skip it
        {
            if (pos + 1 >= length)
            {
                break;
            }
            pos += 3u + descriptor[pos + 1];
            continue;
        }

        uint32_t size = prefix & 3u;
        size = size == 3 ? 4 : size;
        uint32_t type = (prefix >> 2) & 3u;
        uint32_t tag = prefix >> 4;
        if (pos + 1 + size > length)
        {
            break;
        }
        const uint8_t *data = descriptor + pos + 1;
        pos += 1 + size;

        if (type == TYPE_GLOBAL)
        {
            switch (tag)
            {
            case GLOBAL_PAGE:
                page = item_unsigned(data, size);
                break;
            case GLOBAL_LOG_MIN:
                log_min = item_signed(data, size);
                break;
            case GLOBAL_LOG_MAX:
                log_max = item_signed(data, size);
                // a maximum like 0xFF with a minimum of 0 is unsigned
                if (log_min >= 0 && log_max < 0)
                {
                    log_max = (int32_t)item_unsigned(data, size);
                }
                break;
            case GLOBAL_SIZE:
                report_size = item_unsigned(data, size);
                break;
            case GLOBAL_COUNT:
                report_count = item_unsigned(data, size);
                break;
            case GLOBAL_ID:
                report_id = item_unsigned(data, size);
                offset = 0; // each report counts its own bits
                break;
            default:
                break;
            }
            continue;
        }

        if (type == TYPE_LOCAL)
        {
            uint32_t value = item_unsigned(data, size);
            uint32_t full = size == 4 ? value : (page << 16) | value;
            switch (tag)
            {
            case LOCAL_USAGE:
                if (usage_count < MAX_USAGES)
                {
                    usages[usage_count++] = full;
                }
                break;
            case LOCAL_USAGE_MIN:
                usage_min = full;
                range = true;
                break;
            case LOCAL_USAGE_MAX:
                usage_max = full;
                range = true;
                break;
            default:
                break;
            }
            continue;
        }

        if (type == TYPE_MAIN && tag == MAIN_INPUT)
        {
            uint32_t flags = item_unsigned(data, size);
            for (uint32_t i = 0; i < report_count; i++)
            {
                uint32_t usage = 0;
                if (i < usage_count)
                {
                    usage = usages[i];
                }
                else if (range && usage_min + i <= usage_max)
                {
                    usage = usage_min + i;
                }
                else if (usage_count > 0)
                {
                    usage = usages[usage_count - 1]; // the last usage repeats
                }

                uint32_t field_page = usage >> 16;
                uint32_t field_usage = usage & 0xFFFFu;
                uint32_t bit = offset + i * report_size;
                bool same_report = !found_id || report_id == out->report_id;

                if (!(flags & INPUT_CONSTANT) && same_report)
                {
                    hid_field_t *field = NULL;
                    if (field_page == PAGE_DESKTOP && field_usage == USAGE_X)
                    {
                        field = &out->x;
                        out->absolute = !(flags & INPUT_RELATIVE);
                    }
                    else if (field_page == PAGE_DESKTOP && field_usage == USAGE_Y)
                    {
                        field = &out->y;
                    }
                    else if (field_page == PAGE_DESKTOP && field_usage == USAGE_WHEEL)
                    {
                        field = &out->wheel;
                    }
                    else if (field_page == PAGE_BUTTON && field_usage >= 1 && out->buttons < 8)
                    {
                        if (out->buttons == 0)
                        {
                            out->buttons_offset = (uint16_t)bit;
                        }
                        out->buttons++;
                    }

                    if (field && !field->present)
                    {
                        field->present = true;
                        field->offset = (uint16_t)bit;
                        field->size = (uint8_t)report_size;
                        field->min = log_min;
                        field->max = log_max;
                        if (!found_id)
                        {
                            found_id = true;
                            out->report_id = (uint8_t)report_id;
                        }
                    }
                }
            }
            offset += report_size * report_count;
        }

        // a main item (input, output, feature, collection) ends the locals
        if (type == TYPE_MAIN)
        {
            usage_count = 0;
            range = false;
        }
    }

    return out->x.present && out->y.present;
}

int32_t hid_field_value(const hid_field_t *field, const uint8_t *report, uint32_t length)
{
    uint32_t value = 0;
    for (uint32_t i = 0; i < field->size && i < 32; i++)
    {
        uint32_t bit = field->offset + i;
        if (bit / 8 < length && (report[bit / 8] & (1u << (bit % 8))))
        {
            value |= 1u << i;
        }
    }
    if (field->min < 0 && field->size > 0 && field->size < 32 &&
        (value & (1u << (field->size - 1))))
    {
        value |= ~0u << field->size;
    }
    return (int32_t)value;
}
