#pragma once

#include "common/stdint.h"

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
 * read a 16-bit word from the specified I/O port.
 * @param port the I/O port number.
 * @return the word value read from the port.
 */
static inline uint16_t io_inw(uint16_t port)
{
    uint16_t value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/**
 * read count 16-bit words from an I/O port into a buffer (string I/O).
 * @param port       the I/O port to read from.
 * @param buffer     destination buffer (must be at least word_count * 2 bytes).
 * @param word_count number of 16-bit words to read.
 */
static inline void io_insw(uint16_t port, void *buffer, uint32_t word_count)
{
    __asm__ __volatile__("cld; rep insw"
                         : "+D"(buffer), "+c"(word_count)
                         : "d"(port)
                         : "memory");
}

/**
 * write count 16-bit words from a buffer to an I/O port (string I/O).
 * @param port       the I/O port to write to.
 * @param buffer     source buffer.
 * @param word_count number of 16-bit words to write.
 */
static inline void io_outsw(uint16_t port, const void *buffer, uint32_t word_count)
{
    __asm__ __volatile__("cld; rep outsw"
                         : "+S"(buffer), "+c"(word_count)
                         : "d"(port));
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
