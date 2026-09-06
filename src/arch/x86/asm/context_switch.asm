; context_switch.asm switch kernel execution context between two processes.
;
; void arch_context_switch(uint32_t *old_sp, uint32_t new_sp);
;
; saves the current CPU state (callee-saved registers + return address)
; onto the current kernel stack, stores the resulting ESP into *old_sp,
; then loads new_sp and pops the saved state from the new stack.
;
; called from C with the cdecl calling convention:
;   [esp+4] = pointer to old process's kernel_esp field
;   [esp+8] = new process's kernel_esp value
;
; after the switch, execution continues at wherever the new process
; last called (or was set up to return to) arch_context_switch.

BITS 32

global arch_context_switch

arch_context_switch:
    ; save callee-saved registers on the current stack
    push ebp
    push ebx
    push esi
    push edi

    ; save current ESP into *old_sp
    mov eax, [esp + 20]     ; first argument: uint32_t *old_sp
    mov [eax], esp          ; *old_sp = current ESP

    ; load new stack
    mov esp, [esp + 24]     ; second argument: new_sp
                            ; NOTE: this is still at offset 24 from original ESP
                            ; but we just changed ESP, so we're on the new stack now

    ; restore callee-saved registers from the new stack
    pop edi
    pop esi
    pop ebx
    pop ebp

    ; return to wherever the new process was (its saved EIP is on its stack)
    ret
