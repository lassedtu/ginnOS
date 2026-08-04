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
KERNEL_SECTORS      equ 12
KERNEL_START_SECTOR equ 6        ; sector 1: stage1, sectors 2-5: stage2, kernel starts at 6

_start:
    cli
    xor ax, ax
    mov ds, ax
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
    push eax
    call cstart_
    add sp, 4

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
