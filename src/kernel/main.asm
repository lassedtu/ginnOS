; src/kernel/main.asm
; 32-bit kernel entry called by stage2.

BITS 32

section .text
global _start
extern cstart

_start:
    mov eax, [esp + 4]
    mov esp, stack_top
    push eax
    call cstart

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 4096
stack_top:
