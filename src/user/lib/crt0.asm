; crt0.asm — user program entry point for ginnOS.
; calls main(), then invokes SYS_exit with the return value.

BITS 32

extern main
global _start

_start:
    call main          ; call int main(void)

    ; SYS_exit(return value from main is in EAX)
    mov ebx, eax      ; exit code = main's return value
    mov eax, 0        ; SYS_EXIT = 0
    int 0x80          ; syscall

    ; unreachable
    jmp $
