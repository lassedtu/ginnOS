; flushes the GDT by loading the new GDT pointer into the GDTR register

BITS 32

global gdt_flush


gdt_flush:

    mov eax, [esp + 4] ; load the address of the GDT descriptor from the stack

    lgdt [eax]         ; load the GDT descriptor into the GDTR register


    mov ax, 0x10       ; load the data segment selector (0x10) into AX

                       ; set the data segment registers to the new data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax


    jmp 0x08:.flush     ; jump to the code segment selector (0x08) and flush the instruction pipeline

.flush:

    ret