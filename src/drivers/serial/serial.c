#include "serial.h"

#include "arch/x86/cpu/io.h"

// COM1 base I/O port and its register offsets (16550 UART).
enum
{
    COM1_PORT = 0x3F8,

    UART_DATA = 0,          // data register (DLAB=0): read/write a byte
    UART_INT_ENABLE = 1,    // interrupt enable (DLAB=0)
    UART_DIVISOR_LOW = 0,   // baud divisor low byte (DLAB=1)
    UART_DIVISOR_HIGH = 1,  // baud divisor high byte (DLAB=1)
    UART_FIFO_CTRL = 2,     // FIFO control
    UART_LINE_CTRL = 3,     // line control (word length, DLAB)
    UART_MODEM_CTRL = 4,    // modem control
    UART_LINE_STATUS = 5,   // line status
};

// selected register bit values.
enum
{
    LINE_CTRL_8BITS = 0x03,        // 8 data bits, no parity, 1 stop bit
    LINE_CTRL_DLAB = 0x80,         // divisor latch access bit
    FIFO_ENABLE_CLEAR = 0xC7,      // enable FIFO, clear rx/tx, 14-byte threshold
    MODEM_READY = 0x0B,            // DTR + RTS + OUT2 (OUT2 gates the IRQ line)
    LINE_STATUS_TX_EMPTY = 0x20,   // transmit holding register empty
    BAUD_DIVISOR_115200 = 0x0001,  // 115200 baud (115200 / 115200)
};

void serial_initialize(void)
{
    // disable UART interrupts: this driver is polling-only.
    io_outb(COM1_PORT + UART_INT_ENABLE, 0x00);

    // set the baud rate divisor (requires DLAB=1).
    io_outb(COM1_PORT + UART_LINE_CTRL, LINE_CTRL_DLAB);
    io_outb(COM1_PORT + UART_DIVISOR_LOW, BAUD_DIVISOR_115200 & 0xFF);
    io_outb(COM1_PORT + UART_DIVISOR_HIGH, (BAUD_DIVISOR_115200 >> 8) & 0xFF);

    // 8N1, and clear DLAB so the data register is addressable again.
    io_outb(COM1_PORT + UART_LINE_CTRL, LINE_CTRL_8BITS);

    // enable and clear the FIFOs.
    io_outb(COM1_PORT + UART_FIFO_CTRL, FIFO_ENABLE_CLEAR);

    // mark the line ready.
    io_outb(COM1_PORT + UART_MODEM_CTRL, MODEM_READY);
}

/**
 * spin until the transmit holding register can accept another byte.
 */
static void wait_for_tx_ready(void)
{
    while ((io_inb(COM1_PORT + UART_LINE_STATUS) & LINE_STATUS_TX_EMPTY) == 0)
    {
        // busy-wait: transmit is fast and this path is debug-only.
    }
}

void serial_putchar(char c)
{
    // translate newlines so serial terminals advance and return correctly.
    if (c == '\n')
    {
        wait_for_tx_ready();
        io_outb(COM1_PORT + UART_DATA, '\r');
    }

    wait_for_tx_ready();
    io_outb(COM1_PORT + UART_DATA, (uint8_t)c);
}

void serial_write(const char *str)
{
    while (*str)
    {
        serial_putchar(*str);
        str++;
    }
}
