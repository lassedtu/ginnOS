; loads the IDT descriptor into the IDTR register.
; void idt_load(uint32_t idt_ptr_addr)

BITS 32

global idt_load


idt_load:

    mov eax, [esp + 4] ; load the address of the IDT descriptor from the stack

    lidt [eax]         ; load the IDT descriptor into the IDTR register

    ret
