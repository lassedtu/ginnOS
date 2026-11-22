; src/user/lib/i686/crti.asm  the start of _init and _fini (i686).
; GCC puts the code of crtbegin.o (static constructors) between this file
; and crtn.asm, which ends the two functions. only the ginnOS toolchain
; (make sysroot) links these files.

BITS 32

section .init progbits alloc exec nowrite align=4
global _init
_init:
    push ebp
    mov ebp, esp

section .fini progbits alloc exec nowrite align=4
global _fini
_fini:
    push ebp
    mov ebp, esp
