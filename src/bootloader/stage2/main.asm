; Stage2 loader:
;   set up segments & stack
;   switch to 32-bit protected mode
;   populate boot_info_t
;   call cstart_() in C to load kernel from EXT2 filesystem and execute it

BITS 16

section .text
global _start
extern cstart_

_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7A00

    xor eax, eax
    mov ax, 0x7A00
    mov esp, eax
    mov ebp, eax

    mov [boot_info], dl

    ; Switch to 32-bit protected mode
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax
    jmp 0x08:protected_mode_entry

BITS 32
protected_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    mov ebp, esp

    push dword boot_info
    call cstart_
    add esp, 4

realmode_hang:
    cli
    hlt
    jmp realmode_hang

boot_info: db 0

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
