; src/user/lib/x86_64/crtn.asm  the end of _init and _fini (x86_64), after
; crti.asm and crtbegin.o.

BITS 64

section .init progbits alloc exec nowrite align=4
    pop rbp
    ret

section .fini progbits alloc exec nowrite align=4
    pop rbp
    ret
