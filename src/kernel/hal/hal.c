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
    /*
     * Initialization order is strict and must not be changed:
     *
     *  1. gdt_initialize()       — segment selectors must exist before IDT gates
     *                              can reference GDT_CODE_SEGMENT.
     *  2. idt_initialize()       — loads the IDTR; the table must be in place
     *                              before any stubs are installed into it.
     *  3. isr_initialize()       — installs all 256 ISR stubs. Gates are left
     *                              not-present until a handler is registered.
     *  4. exception_initialize() — registers handlers for vectors 0–31 and
     *                              marks those gates present.
     *  5. irq_initialize()       — remaps the PIC and registers irq_handler on
     *                              vectors 32–47, marking those gates present.
     *  6. Device drivers         — register their specific IRQ handlers via
     *                              irq_register_handler before interrupts fire.
     *
     * io_enable_interrupts() (STI) is called by the caller after this function
     * returns. No interrupt can be safely serviced before that point.
     */
    gdt_initialize();
    idt_initialize();
    isr_initialize();
    exception_initialize();
    irq_initialize();
    pit_initialize(100); // initialize PIT with 100 Hz frequency
    keyboard_initialize();
}
