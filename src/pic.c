#include "pic.h"

static unsigned char pic_read_data(unsigned short port)
{
    unsigned char value;

    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void pic_write_data(unsigned short port, unsigned char value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

void pic_send_eoi(unsigned char irq)
{
    if (irq >= 8)
    {
        pic_write_data(0xA0, 0x20);
    }

    pic_write_data(0x20, 0x20);
}

void pic_init(void)
{
    unsigned char master_mask = pic_read_data(0x21);
    unsigned char slave_mask = pic_read_data(0xA1);

    pic_write_data(0x20, 0x11);
    pic_write_data(0xA0, 0x11);
    pic_write_data(0x21, 0x20);
    pic_write_data(0xA1, 0x28);
    pic_write_data(0x21, 0x04);
    pic_write_data(0xA1, 0x02);
    pic_write_data(0x21, 0x01);
    pic_write_data(0xA1, 0x01);

    pic_write_data(0x21, (unsigned char)(master_mask & 0xFD));
    pic_write_data(0xA1, slave_mask);
}