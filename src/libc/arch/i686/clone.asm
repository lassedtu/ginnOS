; src/libc/arch/i686/clone.asm  start a thread (i686).
;
; int __clone(int (*fn)(void *), void *stack, int flags, void *arg,
;             pid_t *parent_tid, void *tls, pid_t *child_tid);
;
; makes a thread with SYS_CLONE (kernel/syscall/sys_proc.c). the new
; thread starts on stack, calls fn(arg), and ends with SYS_EXIT and fn's
; result. the caller gets the new thread's id, or a negative errno.
;
; the kernel wants the number in eax and flags, stack, parent_tid,
; child_tid, tls in ebx, ecx, edx, esi, edi.

BITS 32

SYS_EXIT equ 0                  ; SYS_EXIT in syscall.h
SYS_CLONE equ 97                ; SYS_CLONE in syscall.h

global __clone

__clone:
    push ebp
    push ebx
    push esi
    push edi
    ; the arguments are now at esp + 20 (fn) to esp + 44 (child_tid)

    ; the new stack: arg at a 16-byte boundary and fn above it, so the
    ; thread's call is made with the stack aligned and arg as its argument
    mov ecx, [esp + 24]         ; stack
    and ecx, -16
    sub ecx, 16
    mov edx, [esp + 20]         ; fn
    mov [ecx + 4], edx
    mov edx, [esp + 32]         ; arg
    mov [ecx], edx

    mov eax, SYS_CLONE
    mov ebx, [esp + 28]         ; flags
    mov edx, [esp + 36]         ; parent_tid
    mov edi, [esp + 40]         ; tls
    mov esi, [esp + 44]         ; child_tid
    int 0x80
    test eax, eax
    jnz .caller

    ; the new thread, on its own stack
    xor ebp, ebp                ; the last frame
    mov eax, [esp + 4]          ; fn
    call eax
    mov ebx, eax
    mov eax, SYS_EXIT
    int 0x80
    jmp $                       ; not reached

.caller:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
