; src/libc/arch/x86_64/syscall.asm  the raw syscall for ginnOS libc (x86_64).
;
; long _syscall(long num, long arg1, long arg2, long arg3, long arg4,
;               long arg5);
;
; the C arguments come in rdi, rsi, rdx, rcx, r8, r9 (SysV ABI). the kernel
; wants the number in rax and the arguments in rdi, rsi, rdx, r10, r8
; (kernel/syscall/syscall_internal.h). the syscall instruction changes rcx
; and r11. the result is in rax.

BITS 64

global _syscall

_syscall:
    mov rax, rdi                ; number
    mov rdi, rsi                ; arg1
    mov rsi, rdx                ; arg2
    mov rdx, rcx                ; arg3
    mov r10, r8                 ; arg4
    mov r8, r9                  ; arg5
    syscall
    ret


; void __sigreturn_restorer(void);
;
; where a signal handler returns to: the kernel puts this address on the
; stack as the handler's return address (sa_restorer). it asks the kernel
; to restore the registers saved before the handler ran, and never returns.

SYS_SIGRETURN equ 43            ; SYS_SIGRETURN in syscall.h

global __sigreturn_restorer

__sigreturn_restorer:
    mov rax, SYS_SIGRETURN
    syscall
    jmp $                       ; not reached
