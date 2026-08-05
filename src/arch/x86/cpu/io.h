#pragma once

#include "../../../common/stdint.h"

/**
 * write a byte to the specified I/O port.
 * @param port the I/O port number.
 * @param value the byte value to write.
 */
static inline void io_outb(uint16_t port, uint8_t value)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * read a byte from the specified I/O port.
 * @param port the I/O port number.
 * @return the byte value read from the port.
 */
static inline uint8_t io_inb(uint16_t port)
{
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/**
 * brief delay by writing to an unused I/O port (0x80).
 * used to give slow devices time to catch up after a port write.
 */
void io_wait(void);

/**
 * enable hardware interrupts by executing the STI instruction.
 */
static inline void io_enable_interrupts(void)
{
    __asm__ __volatile__("sti");
}

/**
 * disable hardware interrupts by executing the CLI instruction.
 */
static inline void io_disable_interrupts(void)
{
    __asm__ __volatile__("cli");
}
