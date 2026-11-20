; src/kernel/main.asm
; 32-bit kernel entry called by stage2.

BITS 32

section .text
global _start
extern cstart
extern __bss_start
extern __bss_end

_start:
    mov eax, [esp + 4]          ; boot_info_t physical address from stage2
    mov esp, stack_top

    ; .bss is not part of kernel.bin, so whatever stage2 or the firmware
    ; left in that memory is still there: zero it before any C runs.
    ; (eax is saved in edx; rep stosb uses eax, ecx and edi.)
    mov edx, eax
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb
    mov eax, edx

    push eax
    ; a zero frame pointer marks the outermost frame for backtraces.
    xor ebp, ebp
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
