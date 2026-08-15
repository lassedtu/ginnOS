; flushes the GDT by loading the new GDT pointer into the GDTR register,
; reloads segment registers with kernel selectors, and loads the TSS.

BITS 32

global gdt_flush
global tss_flush


gdt_flush:

    mov eax, [esp + 4] ; load the address of the GDT descriptor from the stack

    lgdt [eax]         ; load the GDT descriptor into the GDTR register


    mov ax, 0x10       ; GDT_KERNEL_DATA selector

    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax


    jmp 0x08:.flush    ; GDT_KERNEL_CODE selector flush instruction pipeline

.flush:

    ret


; void tss_flush(void)
; loads the TSS selector into the Task Register.

tss_flush:

    mov ax, 0x28       ; GDT_TSS selector

    ltr ax

    ret
