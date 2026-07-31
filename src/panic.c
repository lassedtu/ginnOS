#include "panic.h"
#include "vga.h"

static void halt_forever(void)
{
    __asm__ volatile("cli");

    for (;;)
    {
        __asm__ volatile("hlt");
    }
}

void panic(const char *message)
{
    vga_clear();
    vga_write_line("KERNEL PANIC");
    vga_write_line(message);
    halt_forever();
}