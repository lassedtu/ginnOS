; context_switch.asm switch kernel execution context between two processes.
;
; void context_switch(uint32_t *old_esp, uint32_t new_esp);
;
; Saves the current CPU state (callee-saved registers + return address)
; onto the current kernel stack, stores the resulting ESP into *old_esp,
; then loads new_esp and pops the saved state from the new stack.
;
; This function is called from C with the cdecl calling convention:
;   [esp+4] = pointer to old process's kernel_esp field
;   [esp+8] = new process's kernel_esp value
;
; After the switch, execution continues at wherever the new process
; last called (or was set up to return to) context_switch.

BITS 32

global context_switch

context_switch:
    ; save callee-saved registers on the current stack
    push ebp
    push ebx
    push esi
    push edi

    ; save current ESP into *old_esp
    mov eax, [esp + 20]     ; first argument: uint32_t *old_esp
    mov [eax], esp          ; *old_esp = current ESP

    ; load new stack
    mov esp, [esp + 24]     ; second argument: new_esp
                            ; NOTE: this is still at offset 24 from original ESP
                            ; but we just changed ESP, so we're on the new stack now

    ; restore callee-saved registers from the new stack
    pop edi
    pop esi
    pop ebx
    pop ebp

    ; return to wherever the new process was (its saved EIP is on its stack)
    ret
