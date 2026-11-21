; src/arch/x86/asm64/syscall.asm
; the entry for the syscall instruction (x86_64).
;
; syscall puts the user RIP in RCX and the user RFLAGS in R11, loads the
; kernel CS and SS from STAR, clears the flags in SFMASK (IF among them) and
; jumps here. it does not change RSP. this entry changes to the kernel stack
; of the process (syscall_kernel_rsp, set with the TSS RSP0), makes the same
; frame as an interrupt from user mode (vector SYSCALL_VECTOR), and goes on
; in isr_common. the return is with iretq, the same as for an interrupt, so
; fork, signals and execve work on one kind of frame.
;
; the kernel is uniprocessor: one scratch word holds the user RSP until it
; is on the kernel stack, and interrupts stay off until then.

BITS 64

GDT_USER_DATA  equ 0x1B         ; gdt.h
GDT_USER_CODE  equ 0x23
SYSCALL_VECTOR equ 0x80         ; kernel/syscall/syscall.h

extern isr_common
extern syscall_kernel_rsp

global syscall_entry

section .text

syscall_entry:
    mov [rel user_rsp], rsp
    mov rsp, [rel syscall_kernel_rsp]

    push qword GDT_USER_DATA    ; ss
    push qword [rel user_rsp]   ; rsp
    push r11                    ; rflags
    push qword GDT_USER_CODE    ; cs
    push rcx                    ; rip
    push qword 0                ; error code
    push qword SYSCALL_VECTOR   ; vector number

    ; the frame is on the kernel stack: interrupts can come again, as with
    ; the int 0x80 trap gate on i686
    sti
    jmp isr_common

section .bss
align 8
user_rsp:
    resq 1
