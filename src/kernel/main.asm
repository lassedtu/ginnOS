; src/kernel/main.asm
; 32-bit kernel entry called by stage2.

BITS 32

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 4096
stack_top:
