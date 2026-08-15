; syscall.asm  raw syscall entry point for ginnOS libc.
;
; int _syscall(int num, int arg1, int arg2, int arg3, int arg4, int arg5);
;
; Maps C arguments to the register-based syscall convention:
;   EAX = syscall number
;   EBX = arg1
;   ECX = arg2
;   EDX = arg3
;   ESI = arg4
;   EDI = arg5
;
; Returns the result in EAX.

BITS 32

global _syscall

_syscall:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov eax, [ebp + 8]     ; syscall number
    mov ebx, [ebp + 12]    ; arg1
    mov ecx, [ebp + 16]    ; arg2
    mov edx, [ebp + 20]    ; arg3
    mov esi, [ebp + 24]    ; arg4
    mov edi, [ebp + 28]    ; arg5

    int 0x80

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
