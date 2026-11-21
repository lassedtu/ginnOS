; src/libc/arch/x86_64/setjmp.asm  setjmp and longjmp for ginnOS libc
; (x86_64, SysV ABI).
;
; jmp_buf layout (8-byte words): [0] rbx, [1] rbp, [2] r12, [3] r13,
; [4] r14, [5] r15, [6] rsp after the return, [7] the return address.

BITS 64

global setjmp
global longjmp

setjmp:
    mov [rdi], rbx
    mov [rdi + 8], rbp
    mov [rdi + 16], r12
    mov [rdi + 24], r13
    mov [rdi + 32], r14
    mov [rdi + 40], r15
    lea rdx, [rsp + 8]          ; the caller's rsp after setjmp returns
    mov [rdi + 48], rdx
    mov rdx, [rsp]              ; the return address
    mov [rdi + 56], rdx
    xor eax, eax                ; setjmp returns 0
    ret

longjmp:
    mov eax, esi                ; value
    test eax, eax
    jnz .value_ready
    inc eax                     ; longjmp(env, 0) makes setjmp return 1
.value_ready:
    mov rbx, [rdi]
    mov rbp, [rdi + 8]
    mov r12, [rdi + 16]
    mov r13, [rdi + 24]
    mov r14, [rdi + 32]
    mov r15, [rdi + 40]
    mov rsp, [rdi + 48]
    jmp [rdi + 56]
