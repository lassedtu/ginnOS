#include "ktest.h"

#include "common/div64.h"
#include "kernel/time/clock.h"
#include "kernel/time/timer.h"

KTEST(udiv64_32_divides_large_numbers)
{
    uint32_t rest;
    KEXPECT_EQ(udiv64_32(10, 3, &rest), 3);
    KEXPECT_EQ(rest, 1);

    // larger than 32 bits: 2^40 + 5 by 1000
    uint64_t big = (1ull << 40) + 5;
    uint64_t quotient = udiv64_32(big, 1000, &rest);
    KEXPECT(quotient == 1099511627ull);
    KEXPECT_EQ(rest, 781);
}

KTEST(clock_converts_ticks_and_milliseconds)
{
    struct ktimespec span;
    clock_ticks_to_timespec(TIMER_HZ * 2 + TIMER_HZ / 2, &span);
    KEXPECT_EQ(span.tv_sec, 2);
    KEXPECT_EQ(span.tv_nsec, 500000000);

    // a part of a tick rounds up to a whole one
    KEXPECT_EQ(timer_ms_to_ticks(0), 0);
    KEXPECT_EQ(timer_ms_to_ticks(1), 1);
    KEXPECT_EQ(timer_ms_to_ticks(1000), TIMER_HZ);
    KEXPECT_EQ(timer_ms_to_ticks(1001), TIMER_HZ + 1);

    // the wall clock is past 2020 when the RTC is set (QEMU sets it)
    KEXPECT(clock_realtime_seconds() > 1577836800u);
}
