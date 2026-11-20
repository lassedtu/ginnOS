#include "double_fault.h"

#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "kernel/memory/kstack.h"
#include "kernel/panic.h"
#include "kernel/process/process.h"

// stack the double-fault task runs on; the panic path prints through the
// console and walks the faulting stack, so give it room.
#define DOUBLE_FAULT_STACK_SIZE 16384u

static uint8_t double_fault_stack[DOUBLE_FAULT_STACK_SIZE] __attribute__((aligned(16)));

// panic message, built at fault time ("kernel stack overflow (pid N)")
static char message[48];

/**
 * append a decimal number to a string at position pos.
 * @return the new end position.
 */
static uint32_t append_decimal(char *buf, uint32_t pos, uint32_t value)
{
    char digits[10];
    uint32_t count = 0;

    do
    {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);

    while (count > 0)
    {
        buf[pos++] = digits[--count];
    }
    return pos;
}

/**
 * append a string to a buffer at position pos.
 * @return the new end position.
 */
static uint32_t append_string(char *buf, uint32_t pos, const char *str)
{
    while (*str)
    {
        buf[pos++] = *str++;
    }
    return pos;
}

/**
 * entry point of the double-fault task. the CPU arrives here on the task's
 * own stack with interrupts off; the interrupted state is in the main TSS.
 * never returns.
 */
static void double_fault_task(void)
{
    uint32_t eip;
    uint32_t esp;
    uint32_t ebp;
    tss_interrupted_state(&eip, &esp, &ebp);

    // the page fault that escalated left its address in CR2
    uintptr_t fault_address = paging_fault_address();

    if (kstack_is_guard(fault_address) || kstack_is_guard(esp))
    {
        process_t *proc = process_current();

        uint32_t pos = append_string(message, 0, "kernel stack overflow");
        if (proc)
        {
            pos = append_string(message, pos, " (pid ");
            pos = append_decimal(message, pos, proc->pid);
            pos = append_string(message, pos, ")");
        }
        message[pos] = '\0';

        kernel_panic_at(message, eip, 0, ebp);
    }

    kernel_panic_at("double fault", eip, 0, ebp);
}

void double_fault_initialize(void)
{
    tss_install_double_fault(double_fault_task,
                             (uintptr_t)double_fault_stack + DOUBLE_FAULT_STACK_SIZE);

    // a task gate ignores the offset; the TSS selector names the task
    idt_set_gate(8, 0, GDT_DOUBLE_FAULT_TSS,
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_TASK);
}
