; src/libc/arch/x86_64/clone.asm  start a thread (x86_64).
;
; int __clone(int (*fn)(void *), void *stack, int flags, void *arg,
;             pid_t *parent_tid, void *tls, pid_t *child_tid);
;
; makes a thread with SYS_CLONE (kernel/syscall/sys_proc.c). the new
; thread starts on stack, calls fn(arg), and ends with SYS_EXIT and fn's
; result. the caller gets the new thread's id, or a negative errno.
;
; C arguments: rdi fn, rsi stack, edx flags, rcx arg, r8 parent_tid,
; r9 tls, [rsp + 8] child_tid. the kernel wants the number in rax and
; flags, stack, parent_tid, child_tid, tls in rdi, rsi, rdx, r10, r8.

BITS 64

SYS_EXIT equ 0                  ; SYS_EXIT in syscall.h
SYS_CLONE equ 97                ; SYS_CLONE in syscall.h

global __clone

__clone:
    ; fn and arg go on the new stack, which is 16-byte aligned after the
    ; thread takes them off
    and rsi, -16
    sub rsi, 16
    mov [rsi], rdi              ; fn
    mov [rsi + 8], rcx          ; arg

    mov r10, [rsp + 8]          ; child_tid
    mov rax, SYS_CLONE
    mov edi, edx                ; flags
    mov rdx, r8                 ; parent_tid
    mov r8, r9                  ; tls
    syscall
    test rax, rax
    jnz .caller

    ; the new thread, on its own stack
    xor ebp, ebp                ; the last frame
    pop rax                     ; fn
    pop rdi                     ; arg
    call rax
    mov edi, eax
    mov rax, SYS_EXIT
    syscall
    jmp $                       ; not reached

.caller:
    ret
