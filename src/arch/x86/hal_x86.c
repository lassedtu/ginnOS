#include "kernel/hal/hal.h"

#include "arch/x86/cpu/gdt.h"
#include "arch/x86/cpu/idt.h"
#include "arch/x86/cpu/isr.h"
#include "arch/x86/cpu/exception.h"
#include "arch/x86/cpu/irq.h"
#include "drivers/pit/pit.h"
#include "drivers/keyboard/keyboard.h"

/**
 * @file hal_x86.c
 * @brief x86 implementation of the hardware abstraction layer.
 *
 * the portable kernel calls hal_initialize() without knowing anything about
 * the GDT, IDT, PIC, or which drivers exist. all of that lives here, behind
 * the neutral hal.h interface, so another architecture can supply its own
 * hal_x86.c-equivalent (GIC on ARM, PLIC on RISC-V) without touching the
 * kernel.
 */

void hal_initialize(void)
{
    /*
     * initialization order is strict. each step depends on the one before it
     * and must not be reordered.
     *
     *  1. gdt_initialize()      , must run first. every IDT gate entry embeds
     *                              a GDT segment selector (GDT_KERNEL_CODE).
     *                              if the GDT is not loaded, those selectors
     *                              are meaningless and the CPU will fault the
     *                              moment it tries to dispatch an interrupt.
     *
     *  2. idt_initialize()      , loads the IDTR register with the address of
     *                              the 256-entry IDT. the table must be in
     *                              place before any gates are written into it.
     *
     *  3. isr_initialize()      , calls isr_init_gates(), which writes a stub
     *                              address into every IDT entry but leaves all
     *                              gates marked not-present. no interrupt can
     *                              be dispatched yet.
     *
     *  4. exception_initialize(), registers a handler for each CPU exception
     *                              (vectors 0–31) and marks those gates present.
     *                              must follow isr_initialize() so the stubs
     *                              are already wired before the gates open.
     *
     *  5. irq_initialize()      , remaps the 8259A PIC so IRQ 0–15 land at
     *                              vectors 32–47 (above the exception range),
     *                              then registers irq_handler on each of those
     *                              vectors and marks them present. must follow
     *                              exception_initialize() so the exception gates
     *                              are already settled before the PIC is live.
     *
     *  6. device drivers        , register their IRQ-specific handlers via
     *                              irq_register_handler() before STI fires.
     *                              drivers that call irq_register_handler() do
     *                              not need to touch the IDT directly.
     *
     * interrupts (STI) are intentionally NOT enabled here. the caller
     * (kernel_main) defers arch_enable_interrupts() until after
     * hal_initialize() returns, so that the full handler chain, stubs,
     * exception handlers, IRQ handlers, and device drivers, is completely in
     * place before the CPU can deliver the first interrupt.
     */
    gdt_initialize();
    idt_initialize();
    isr_initialize();
    exception_initialize();
    irq_initialize();
    pit_initialize(PIT_FREQUENCY_HZ);
    keyboard_initialize();
}
