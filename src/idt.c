#include "idt.h"

#include "keyboard.h"
#include "pic.h"
#include "panic.h"

struct idt_entry
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void isr32(void);
extern void isr33(void);
extern void isr34(void);
extern void isr35(void);
extern void isr36(void);
extern void isr37(void);
extern void isr38(void);
extern void isr39(void);
extern void isr40(void);
extern void isr41(void);
extern void isr42(void);
extern void isr43(void);
extern void isr44(void);
extern void isr45(void);
extern void isr46(void);
extern void isr47(void);

static struct idt_entry idt_entries[256];
static struct idt_ptr idt_descriptor;

static void idt_set_gate(uint8_t vector, void (*handler)(void))
{
    uint32_t handler_address = (uint32_t)handler;

    idt_entries[vector].offset_low = handler_address & 0xFFFF;
    idt_entries[vector].selector = 0x08;
    idt_entries[vector].zero = 0;
    idt_entries[vector].type_attr = 0x8E;
    idt_entries[vector].offset_high = (handler_address >> 16) & 0xFFFF;
}

void idt_init(void)
{
    idt_set_gate(0, isr0);
    idt_set_gate(1, isr1);
    idt_set_gate(2, isr2);
    idt_set_gate(3, isr3);
    idt_set_gate(4, isr4);
    idt_set_gate(5, isr5);
    idt_set_gate(6, isr6);
    idt_set_gate(7, isr7);
    idt_set_gate(8, isr8);
    idt_set_gate(9, isr9);
    idt_set_gate(10, isr10);
    idt_set_gate(11, isr11);
    idt_set_gate(12, isr12);
    idt_set_gate(13, isr13);
    idt_set_gate(14, isr14);
    idt_set_gate(15, isr15);
    idt_set_gate(16, isr16);
    idt_set_gate(17, isr17);
    idt_set_gate(18, isr18);
    idt_set_gate(19, isr19);
    idt_set_gate(20, isr20);
    idt_set_gate(21, isr21);
    idt_set_gate(22, isr22);
    idt_set_gate(23, isr23);
    idt_set_gate(24, isr24);
    idt_set_gate(25, isr25);
    idt_set_gate(26, isr26);
    idt_set_gate(27, isr27);
    idt_set_gate(28, isr28);
    idt_set_gate(29, isr29);
    idt_set_gate(30, isr30);
    idt_set_gate(31, isr31);
    idt_set_gate(32, isr32);
    idt_set_gate(33, isr33);
    idt_set_gate(34, isr34);
    idt_set_gate(35, isr35);
    idt_set_gate(36, isr36);
    idt_set_gate(37, isr37);
    idt_set_gate(38, isr38);
    idt_set_gate(39, isr39);
    idt_set_gate(40, isr40);
    idt_set_gate(41, isr41);
    idt_set_gate(42, isr42);
    idt_set_gate(43, isr43);
    idt_set_gate(44, isr44);
    idt_set_gate(45, isr45);
    idt_set_gate(46, isr46);
    idt_set_gate(47, isr47);

    idt_descriptor.limit = sizeof(idt_entries) - 1;
    idt_descriptor.base = (uint32_t)idt_entries;

    __asm__ volatile("lidt %0" : : "m"(idt_descriptor));
}

void interrupt_handler(uint32_t vector, uint32_t error_code)
{
    (void)error_code;

    switch (vector)
    {
    case 0:
        panic("Divide by zero");
        break;
    case 6:
        panic("Invalid opcode");
        break;
    case 8:
        panic("Double fault");
        break;
    case 13:
        panic("General protection fault");
        break;
    case 14:
        panic("Page fault");
        break;
    default:
        if (vector == 33)
        {
            keyboard_handle_irq();
            pic_send_eoi(1);
            return;
        }

        if (vector >= 32 && vector < 48)
        {
            pic_send_eoi((unsigned char)(vector - 32));
            return;
        }

        panic("Unhandled CPU exception");
        break;
    }
}