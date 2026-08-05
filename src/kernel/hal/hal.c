#include "hal.h"
#include "../../arch/x86/cpu/gdt.h"
#include "../../arch/x86/cpu/idt.h"
#include "../../arch/x86/cpu/isr.h"
#include "../../arch/x86/cpu/irq.h"
#include "../../drivers/keyboard/keyboard.h"

void hal_initialize(void)
{
    gdt_initialize();
    idt_initialize();
    isr_initialize();
    irq_initialize();

    keyboard_initialize();
}
