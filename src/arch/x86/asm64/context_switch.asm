; src/arch/x86/asm64/context_switch.asm
; switch kernel execution from one process to another (x86_64).
;
; void arch_context_switch(uintptr_t *old_sp, uintptr_t new_sp);
;
; saves the callee-saved registers of the SysV ABI (rbp, rbx, r12-r15) on
; the current kernel stack, stores RSP into *old_sp (rdi), loads new_sp
; (rsi) and pops the same registers from the new stack. the 'ret' then
; continues where the new process called this function, or where
; arch_setup_initial_stack / arch_setup_fork_stack set it up to start.

BITS 64

global arch_context_switch

arch_context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, rsi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
