; setjmp.asm  setjmp and longjmp for ginnOS libc (i386 cdecl).
;
; jmp_buf layout: [0] ebx, [4] esi, [8] edi, [12] ebp, [16] esp after the
; return, [20] the return address.

BITS 32

global setjmp
global longjmp

setjmp:
    mov eax, [esp + 4]      ; env
    mov [eax], ebx
    mov [eax + 4], esi
    mov [eax + 8], edi
    mov [eax + 12], ebp
    lea ecx, [esp + 4]      ; the caller's esp after setjmp returns
    mov [eax + 16], ecx
    mov ecx, [esp]          ; the return address
    mov [eax + 20], ecx
    xor eax, eax            ; setjmp returns 0
    ret

longjmp:
    mov edx, [esp + 4]      ; env
    mov eax, [esp + 8]      ; value
    test eax, eax
    jnz .value_ready
    inc eax                 ; longjmp(env, 0) makes setjmp return 1
.value_ready:
    mov ebx, [edx]
    mov esi, [edx + 4]
    mov edi, [edx + 8]
    mov ebp, [edx + 12]
    mov esp, [edx + 16]
    jmp [edx + 20]
