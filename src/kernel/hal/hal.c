#include "hal.h"
#include "../../arch/x86/cpu/gdt.h"
#include "../../arch/x86/cpu/idt.h"
#include "../../arch/x86/cpu/isr.h"
#include "../../arch/x86/cpu/exception.h"
#include "../../arch/x86/cpu/irq.h"
#include "../../drivers/pit/pit.h"
#include "../../drivers/keyboard/keyboard.h"

void hal_initialize(void)
{
    gdt_initialize();
    idt_initialize();
    isr_initialize();
    exception_initialize();
    irq_initialize();
    pit_initialize(100); // initialize PIT with 100 Hz frequency
    keyboard_initialize();
}
