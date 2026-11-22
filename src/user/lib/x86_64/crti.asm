; src/user/lib/x86_64/crti.asm  the start of _init and _fini (x86_64).
; GCC puts the code of crtbegin.o (static constructors) between this file
; and crtn.asm, which ends the two functions. only the ginnOS toolchain
; (make sysroot) links these files.

BITS 64

section .init progbits alloc exec nowrite align=4
global _init
_init:
    push rbp
    mov rbp, rsp

section .fini progbits alloc exec nowrite align=4
global _fini
_fini:
    push rbp
    mov rbp, rsp
