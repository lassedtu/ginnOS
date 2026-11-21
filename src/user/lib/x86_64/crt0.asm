; src/user/lib/x86_64/crt0.asm  user program entry point for ginnOS
; (x86_64). the kernel puts argc, argv and envp on the user stack before it
; jumps here (usermode_build_stack in the kernel).
;
; stack layout on entry (8-byte words):
;   [rsp]      = 0 (fake return address)
;   [rsp + 8]  = argc
;   [rsp + 16] = argv (pointer to a NULL-terminated char* array)
;   [rsp + 24] = envp (pointer to a NULL-terminated char* array)
;
; sets environ, calls main(argc, argv, envp), then exit() with the return
; value (exit runs the atexit handlers).

BITS 64

extern main
extern environ
extern exit
global _start

_start:
    mov rdi, [rsp + 8]          ; argc
    mov rsi, [rsp + 16]         ; argv
    mov rdx, [rsp + 24]         ; envp
    mov [rel environ], rdx      ; the libc environment starts as the one passed in

    xor rbp, rbp                ; the outermost frame
    and rsp, -16                ; the ABI wants rsp % 16 == 0 at a call
    call main                   ; int main(int argc, char **argv, char **envp)

    mov edi, eax                ; exit(main's return value)
    call exit

    jmp $                       ; not reached
