; ISR assembly stubs and common handler.
; defines macros for generating ISR entry points, then includes the
; auto-generated stub invocations and handles the common save/restore logic.

BITS 32

extern isr_handler

; kernel data segment selector, must match GDT_KERNEL_DATA in gdt.h.
%define GDT_KERNEL_DATA 0x10


; macro for interrupts that do NOT push an error code.
; pushes a dummy zero error code so the stack layout is uniform.
%macro ISR_NOERRORCODE 1

global isr_stub_%1
isr_stub_%1:
    push 0              ; push dummy error code
    push %1             ; push interrupt vector number
    jmp isr_common

%endmacro


; macro for interrupts that DO push an error code.
; the CPU has already pushed the error code before entering.
%macro ISR_ERRORCODE 1

global isr_stub_%1
isr_stub_%1:
                        ; CPU already pushed the error code
    push %1             ; push interrupt vector number
    jmp isr_common

%endmacro


; include the auto-generated stub invocations for all 256 vectors
%include "isr_gen.inc"


; common ISR handler, saves register state, calls C handler, restores state.
isr_common:
    pusha               ; push eax, ecx, edx, ebx, esp, ebp, esi, edi

    xor eax, eax        ; save the current data segment selector
    mov ax, ds
    push eax

    mov ax, GDT_KERNEL_DATA ; load kernel data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp            ; pass pointer to registers struct as argument
    call isr_handler
    add esp, 4          ; clean up the argument

    pop eax             ; restore the original data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa                ; restore general-purpose registers
    add esp, 8          ; remove interrupt number and error code from stack
    iret                ; return from interrupt (pops eip, cs, eflags, esp, ss)
