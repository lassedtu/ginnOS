; crt0.asm user program entry point for ginnOS.
; the kernel puts argc, argv and envp on the user stack before it jumps here
; (usermode_build_stack in the kernel).
;
; stack layout on entry:
;   [esp]    = 0 (fake return address)
;   [esp+4]  = argc
;   [esp+8]  = argv (pointer to a NULL-terminated char* array)
;   [esp+12] = envp (pointer to a NULL-terminated char* array)
;
; sets environ, calls main(argc, argv, envp), then invokes SYS_exit with the
; return value.

BITS 32

extern main
extern environ
global _start

_start:
    mov eax, [esp + 4]     ; argc
    mov ebx, [esp + 8]     ; argv
    mov ecx, [esp + 12]    ; envp
    mov [environ], ecx     ; the libc environment starts as the one passed in

    push ecx               ; envp (third argument)
    push ebx               ; argv (second argument)
    push eax               ; argc (first argument)
    call main              ; int main(int argc, char **argv, char **envp)
    add esp, 12            ; clean up arguments

    ; SYS_exit(return value from main is in EAX)
    mov ebx, eax           ; exit code = main's return value
    mov eax, 0             ; SYS_EXIT = 0
    int 0x80               ; syscall

    ; unreachable
    jmp $
