#include "pit.h"

#include "arch/x86/cpu/io.h"
#include "arch/arch_irq.h"
#include "kernel/scheduler/scheduler.h"

enum
{
    PIT_CHANNEL0_DATA = 0x40,     // channel 0 data port
    PIT_COMMAND = 0x43,           // command port
    PIT_INPUT_FREQUENCY = 1193182 // input frequency of the PIT in Hz
};

enum
{
    PIT_COMMAND_BINARY = 0x00,      // 16-bit binary mode
    PIT_COMMAND_MODE3 = 0x06,       // mode 3 (square wave generator)
    PIT_COMMAND_ACCESS_LOHI = 0x30, // access mode: lobyte/hibyte
    PIT_COMMAND_CHANNEL0 = 0x00     // channel 0
};

static volatile uint64_t ticks = 0; // number of PIT interrupts since initialization

/**
 * PIT interrupt handler
 * increments the tick count and notifies the scheduler.
 */
static void pit_irq_handler(uint32_t irq, trap_frame_t *frame)
{
    (void)irq;
    (void)frame;

    ticks++;
    scheduler_tick();
}

void pit_initialize(uint32_t frequency)
{
    uint16_t divisor;

    if (frequency == 0)
    {
        frequency = PIT_FREQUENCY_HZ;
    }

    /* if frequency is very small (e.g. 1 Hz), PIT_INPUT_FREQUENCY / frequency
    can exceed 65535, which overflows the uint16_t*/
    uint32_t raw_divisor = PIT_INPUT_FREQUENCY / frequency;
    if (raw_divisor > 0xFFFF)
        raw_divisor = 0xFFFF;
    divisor = (uint16_t)raw_divisor;

    // register IRQ0 handler
    arch_irq_register(0, pit_irq_handler);

    // enable IRQ0 (timer)
    arch_irq_enable(0);

    uint8_t command =
        PIT_COMMAND_CHANNEL0 |
        PIT_COMMAND_ACCESS_LOHI |
        PIT_COMMAND_MODE3 |
        PIT_COMMAND_BINARY;

    io_outb(PIT_COMMAND, command);

    io_outb(PIT_CHANNEL0_DATA, divisor & 0xFF);
    io_outb(PIT_CHANNEL0_DATA, divisor >> 8);
}

/**
 * returns the number of PIT interrupts since initialization.
 */
uint64_t pit_get_ticks(void)
{
    return ticks;
}