; src/arch/x86/asm64/entry.asm
; the x86_64 kernel entry. limine starts the kernel here in long mode, with
; the kernel image mapped at its link address, the higher-half direct map
; (HHDM) set up, and interrupts off (the limine boot protocol). the stack
; that limine gives is in bootloader-reclaimable memory, so the kernel
; changes to its own stack first. cstart (arch/x86/boot64/limine_boot.c)
; reads the limine responses and calls kernel_main.

BITS 64

section .text
global _start
extern cstart

_start:
    mov rsp, stack_top
    xor rbp, rbp                ; a zero frame pointer ends the backtrace
    call cstart

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
