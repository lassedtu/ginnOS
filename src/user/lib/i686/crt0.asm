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
; sets environ, gives the thread its thread-local storage (__init_tls),
; calls main(argc, argv, envp), then exit() with the return value (exit
; runs the atexit handlers).

BITS 32

extern main
extern environ
extern exit
extern __init_tls
global _start

_start:
    mov esi, [esp + 4]     ; argc
    mov edi, [esp + 8]     ; argv
    mov ebx, [esp + 12]    ; envp
    mov [environ], ebx     ; the libc environment starts as the one passed in

    call __init_tls        ; keeps esi, edi and ebx (the ABI's saved registers)

    push ebx               ; envp (third argument)
    push edi               ; argv (second argument)
    push esi               ; argc (first argument)
    call main              ; int main(int argc, char **argv, char **envp)
    add esp, 12            ; clean up arguments

    push eax               ; exit(main's return value)
    call exit

    ; unreachable
    jmp $
