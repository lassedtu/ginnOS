[BITS 32] ; 32-bit protected mode

global _start
extern kernel_main

_start:
    call kernel_main

    jmp $ ; infinite loop to halt the CPU after kernel_main returns

times 512-($ - $$) db 0 ; fill the rest of the sector with zeros