#include "pic.h"
#include "io.h"

/** PIC I/O port addresses. */
#define PIC1_COMMAND_PORT 0x20 // command port for master PIC
#define PIC1_DATA_PORT 0x21    // data port for master PIC
#define PIC2_COMMAND_PORT 0xA0 // command port for slave PIC
#define PIC2_DATA_PORT 0xA1    // data port for slave PIC

/**
 * ICW1 flags.
 * (bits 5–7 ignored on x86)
 */
enum
{
    PIC_ICW1_ICW4 = 0x01,      // ICW4 needed
    PIC_ICW1_SINGLE = 0x02,    // single PIC (1) or cascaded (0)
    PIC_ICW1_INTERVAL4 = 0x04, // call address interval (ignored on x86)
    PIC_ICW1_LEVEL = 0x08,     // level triggered (1) or edge triggered (0)
    PIC_ICW1_INITIALIZE = 0x10 // initialization flag (must be 1)
};

/**
 * ICW4 flags.
 * (bits 5–7 reserved)
 */
enum
{
    PIC_ICW4_8086 = 0x01,          // 8086/88 mode (1) or MCS-80/85 mode (0)
    PIC_ICW4_AUTO_EOI = 0x02,      // auto EOI (1) or normal EOI (0)
    PIC_ICW4_BUFFER_MASTER = 0x04, // buffered mode master (1) or slave (0)
    PIC_ICW4_BUFFER_SLAVE = 0x00,  // buffered mode slave (1) or master (0)
    PIC_ICW4_BUFFERED = 0x08,      // buffered mode (1) or non-buffered (0)
    PIC_ICW4_SFNM = 0x10           // special fully nested mode (1) or normal EOI (0)
};

/** PIC command bytes. */
enum
{
    PIC_CMD_END_OF_INTERRUPT = 0x20, // end-of-interrupt command code (EOI)
    PIC_CMD_READ_IRR = 0x0A,         // read IRR command code
    PIC_CMD_READ_ISR = 0x0B          // read ISR command code
};

void pic_configure(uint8_t offset_master, uint8_t offset_slave)
{
    // ICW1: begin initialization sequence on both PICs
    io_outb(PIC1_COMMAND_PORT, PIC_ICW1_ICW4 | PIC_ICW1_INITIALIZE);
    io_wait();
    io_outb(PIC2_COMMAND_PORT, PIC_ICW1_ICW4 | PIC_ICW1_INITIALIZE);
    io_wait();

    // ICW2: set the vector offsets
    io_outb(PIC1_DATA_PORT, offset_master);
    io_wait();
    io_outb(PIC2_DATA_PORT, offset_slave);
    io_wait();

    // ICW3: tell master PIC there is a slave PIC at IRQ2 (bit 2 = 0x04)
    io_outb(PIC1_DATA_PORT, 0x04);
    io_wait();

    // ICW3: tell slave PIC its cascade identity (IRQ2 = 0x02)
    io_outb(PIC2_DATA_PORT, 0x02);
    io_wait();

    // ICW4: set 8086 mode on both PICs
    io_outb(PIC1_DATA_PORT, PIC_ICW4_8086);
    io_wait();
    io_outb(PIC2_DATA_PORT, PIC_ICW4_8086);
    io_wait();

    // clear the data registers (unmask all IRQs)
    io_outb(PIC1_DATA_PORT, 0);
    io_wait();
    io_outb(PIC2_DATA_PORT, 0);
    io_wait();
}

void pic_send_eoi(int irq)
{
    // if the IRQ came from the slave PIC, send EOI to the slave first
    if (irq >= 8)
    {
        io_outb(PIC2_COMMAND_PORT, PIC_CMD_END_OF_INTERRUPT);
    }

    // always send EOI to the master PIC
    io_outb(PIC1_COMMAND_PORT, PIC_CMD_END_OF_INTERRUPT);
}

void pic_disable(void)
{
    io_outb(PIC1_DATA_PORT, 0xFF);
    io_wait();
    io_outb(PIC2_DATA_PORT, 0xFF);
    io_wait();
}

void pic_mask(int irq)
{
    uint16_t port;
    uint8_t mask;

    if (irq < 8)
    {
        port = PIC1_DATA_PORT;
    }
    else
    {
        irq -= 8;
        port = PIC2_DATA_PORT;
    }

    mask = io_inb(port);
    io_outb(port, mask | (1 << irq));
}

void pic_unmask(int irq)
{
    uint16_t port;
    uint8_t mask;

    if (irq < 8)
    {
        port = PIC1_DATA_PORT;
    }
    else
    {
        irq -= 8;
        port = PIC2_DATA_PORT;
    }

    mask = io_inb(port);
    io_outb(port, mask & ~(1 << irq));
}

uint16_t pic_read_irr(void)
{
    io_outb(PIC1_COMMAND_PORT, PIC_CMD_READ_IRR);
    io_outb(PIC2_COMMAND_PORT, PIC_CMD_READ_IRR);

    return ((uint16_t)io_inb(PIC2_COMMAND_PORT) << 8) | (uint16_t)io_inb(PIC1_COMMAND_PORT);
}

uint16_t pic_read_isr(void)
{
    io_outb(PIC1_COMMAND_PORT, PIC_CMD_READ_ISR);
    io_outb(PIC2_COMMAND_PORT, PIC_CMD_READ_ISR);

    return ((uint16_t)io_inb(PIC2_COMMAND_PORT) << 8) | (uint16_t)io_inb(PIC1_COMMAND_PORT);
}
