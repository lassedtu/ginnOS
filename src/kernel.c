#include "kernel.h"
#include "idt.h"
#include "pic.h"
#include "vga.h"

void kernel_main()
{
    idt_init();
    pic_init();
    vga_clear();
    vga_write_line("Ginnung booted!");
    vga_write_line("Type on the keyboard to echo characters.");

    __asm__ volatile("sti");

    for (;;)
    {
        __asm__ volatile("hlt");
    }
}