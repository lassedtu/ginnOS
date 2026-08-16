; crt0.asm user program entry point for ginnOS.
; the kernel pushes argc/argv onto the user stack before jumping here.
;
; stack layout on entry:
;   [esp]   = 0 (fake return address)
;   [esp+4] = argc
;   [esp+8] = argv (pointer to char* array)
;
; calls main(argc, argv), then invokes SYS_exit with the return value.

BITS 32

extern main
global _start

_start:
    ; argc and argv are already on the stack in the right position
    ; for a cdecl call to main(int argc, char **argv).
    ; the fake return address at [esp] acts as the return address
    ; slot, so [esp+4] = first arg, [esp+8] = second arg.
    mov eax, [esp + 4]     ; argc
    mov ebx, [esp + 8]     ; argv
    push ebx               ; push argv (second argument)
    push eax               ; push argc (first argument)
    call main              ; call int main(int argc, char **argv)
    add esp, 8             ; clean up arguments

    ; SYS_exit(return value from main is in EAX)
    mov ebx, eax           ; exit code = main's return value
    mov eax, 0             ; SYS_EXIT = 0
    int 0x80               ; syscall

    ; unreachable
    jmp $
