#include "kernel.h"
#include "idt.h"
#include "pic.h"
#include "printk.h"
#include "shell.h"
#include "vga.h"

void kernel_main()
{
    idt_init();
    pic_init();
    vga_clear();
    printk("That was the age when nothing was; / There was no sand, nor sea, nor cool waves, / No earth nor sky nor grass there, / Only Ginnungagap.\n");
    shell_init();

    __asm__ volatile("sti");

    for (;;)
    {
        __asm__ volatile("hlt");
    }
}