; src/bootloader/stage2/main.asm
; Stage2 loader:
; 1) real mode disk load for kernel
; 2) call cstart_() implemented in C while still in real mode
; 3) remain in real mode

BITS 16

section .text
global _start
extern cstart_

KERNEL_LOAD_SEGMENT equ 0x1000   ; physical 0x10000
KERNEL_LOAD_OFFSET  equ 0x0000
KERNEL_SECTORS      equ 48
KERNEL_START_SECTOR equ 18       ; sector 1: stage1, sectors 2-17: stage2, kernel starts at 18

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

    mov [boot_drive], dl

    mov ax, KERNEL_LOAD_SEGMENT
    mov es, ax
    mov bx, KERNEL_LOAD_OFFSET

    mov ah, 0x02                 ; BIOS read sectors
    mov al, KERNEL_SECTORS
    mov ch, 0x00                 ; cylinder 0
    mov cl, KERNEL_START_SECTOR  ; sector index on track
    mov dh, 0x00                 ; head 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Call stage2 C entry in real mode.
    ; The boot drive (DL) is forwarded as an argument.
    xor eax, eax
    mov al, [boot_drive]
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
    mov esp, stage2_stack_top
    mov ebp, esp

    movzx eax, byte [boot_drive]
    push eax
    call cstart_
    add esp, 4

realmode_hang:
    cli
    hlt
    jmp realmode_hang

disk_error:
    cli
.hang:
    hlt
    jmp .hang

boot_drive: db 0

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

section .bss
align 16
stage2_stack:
    resb 16384
stage2_stack_top:
