#include "rtc.h"

#include "arch/arch.h"
#include "arch/x86/cpu/io.h"

/**
 * @file rtc.c
 * @brief CMOS real-time clock (MC146818 compatible).
 *
 * the clock keeps seconds, minutes, hours, day, month and year in CMOS
 * registers, in BCD or binary and 12 or 24 hour form (status register B
 * says which). they change once a second; the registers are read until two
 * reads agree, so a read is never torn by an update.
 */

enum
{
    CMOS_ADDRESS = 0x70,
    CMOS_DATA = 0x71,

    REG_SECONDS = 0x00,
    REG_MINUTES = 0x02,
    REG_HOURS = 0x04,
    REG_DAY = 0x07,
    REG_MONTH = 0x08,
    REG_YEAR = 0x09,
    REG_STATUS_A = 0x0A,
    REG_STATUS_B = 0x0B,
    REG_CENTURY = 0x32, // where most PC firmware keeps it

    STATUS_A_UPDATING = 0x80,
    STATUS_B_BINARY = 0x04,
    STATUS_B_24_HOUR = 0x02,
    HOURS_PM = 0x80,

    // bit 7 of the address port masks the NMI; keep it masked as firmware does
    NMI_DISABLE = 0x80,
};

typedef struct
{
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
} cmos_time_t;

static uint8_t cmos_read(uint8_t reg)
{
    io_outb(CMOS_ADDRESS, (uint8_t)(NMI_DISABLE | reg));
    return io_inb(CMOS_DATA);
}

static void read_registers(cmos_time_t *t)
{
    // wait for the end of an update in progress
    for (int tries = 0; tries < 100000 && (cmos_read(REG_STATUS_A) & STATUS_A_UPDATING); tries++)
    {
    }

    t->seconds = cmos_read(REG_SECONDS);
    t->minutes = cmos_read(REG_MINUTES);
    t->hours = cmos_read(REG_HOURS);
    t->day = cmos_read(REG_DAY);
    t->month = cmos_read(REG_MONTH);
    t->year = cmos_read(REG_YEAR);
    t->century = cmos_read(REG_CENTURY);
}

static bool same_time(const cmos_time_t *a, const cmos_time_t *b)
{
    return a->seconds == b->seconds && a->minutes == b->minutes && a->hours == b->hours &&
           a->day == b->day && a->month == b->month && a->year == b->year &&
           a->century == b->century;
}

static uint8_t from_bcd(uint8_t value)
{
    return (uint8_t)((value >> 4) * 10 + (value & 0x0F));
}

static bool is_leap(uint32_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

/**
 * days from 1970-01-01 to the first of a month.
 */
static uint32_t days_before(uint32_t year, uint32_t month)
{
    static const uint16_t month_starts[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

    uint32_t days = 0;
    for (uint32_t y = 1970; y < year; y++)
    {
        days += is_leap(y) ? 366 : 365;
    }
    days += month_starts[month - 1];
    if (month > 2 && is_leap(year))
    {
        days++;
    }
    return days;
}

bool rtc_read_unix_time(uint32_t *seconds_out)
{
    cmos_time_t now;
    cmos_time_t again;

    uint32_t flags = arch_irq_save();
    read_registers(&now);
    for (int tries = 0; tries < 5; tries++)
    {
        read_registers(&again);
        if (same_time(&now, &again))
        {
            break;
        }
        now = again;
    }
    uint8_t status_b = cmos_read(REG_STATUS_B);
    arch_irq_restore(flags);

    bool pm = (now.hours & HOURS_PM) != 0;
    now.hours &= (uint8_t)~HOURS_PM;
    if (!(status_b & STATUS_B_BINARY))
    {
        now.seconds = from_bcd(now.seconds);
        now.minutes = from_bcd(now.minutes);
        now.hours = from_bcd(now.hours);
        now.day = from_bcd(now.day);
        now.month = from_bcd(now.month);
        now.year = from_bcd(now.year);
        now.century = from_bcd(now.century);
    }
    if (!(status_b & STATUS_B_24_HOUR))
    {
        // 12 hour clock: 12 AM is 0, 12 PM is 12
        now.hours = (uint8_t)(now.hours % 12 + (pm ? 12 : 0));
    }

    uint32_t year = (now.century >= 19 && now.century <= 21) ? now.century * 100u + now.year
                                                             : 2000u + now.year;
    if (now.month < 1 || now.month > 12 || now.day < 1 || now.day > 31 || now.hours > 23 ||
        now.minutes > 59 || now.seconds > 59 || year < 1970)
    {
        return false;
    }

    uint32_t days = days_before(year, now.month) + now.day - 1u;
    *seconds_out = days * 86400u + now.hours * 3600u + now.minutes * 60u + now.seconds;
    return true;
}
