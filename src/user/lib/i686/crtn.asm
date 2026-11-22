; src/user/lib/i686/crtn.asm  the end of _init and _fini (i686), after
; crti.asm and crtbegin.o.

BITS 32

section .init progbits alloc exec nowrite align=4
    pop ebp
    ret

section .fini progbits alloc exec nowrite align=4
    pop ebp
    ret
