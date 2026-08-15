; kernel_setjmp / kernel_longjmp
; minimal setjmp/longjmp for returning from usermode back to the kernel.
;
; jmp_buf layout (6 x uint32_t = 24 bytes):
;   [0] ebx
;   [4] esi
;   [8] edi
;  [12] ebp
;  [16] esp (after return from setjmp i.e., caller's esp)
;  [20] eip (return address where to resume in caller)

BITS 32

global kernel_setjmp
global kernel_longjmp


; int kernel_setjmp(uint32_t *buf)
; saves callee-saved registers + stack pointer + return address.
; returns 0 on initial call.

kernel_setjmp:
    mov eax, [esp + 4]     ; eax = buf pointer

    mov [eax +  0], ebx
    mov [eax +  4], esi
    mov [eax +  8], edi
    mov [eax + 12], ebp

    ; save caller's esp (esp after we return = current esp + 4 for the ret pop)
    lea ecx, [esp + 4]
    mov [eax + 16], ecx

    ; save return address (sitting at top of stack)
    mov ecx, [esp]
    mov [eax + 20], ecx

    xor eax, eax           ; return 0
    ret


; void kernel_longjmp(uint32_t *buf, int val)
; restores saved state and returns to the setjmp call site with val as return value.
; val must not be 0 (if 0 is passed, it is forced to 1).

kernel_longjmp:
    mov edx, [esp + 4]     ; edx = buf pointer
    mov eax, [esp + 8]     ; eax = return value

    test eax, eax          ; if val == 0, force to 1
    jnz .nonzero
    inc eax
.nonzero:

    mov ebx, [edx +  0]
    mov esi, [edx +  4]
    mov edi, [edx +  8]
    mov ebp, [edx + 12]
    mov esp, [edx + 16]

    jmp [edx + 20]        ; jump to saved return address
