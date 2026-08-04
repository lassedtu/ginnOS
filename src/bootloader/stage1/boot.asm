; src/bootloader/stage1/boot.asm
; 16-bit boot sector loaded by BIOS at 0x7C00.
; Loads stage2 from disk and jumps to it.

BITS 16
ORG 0x7C00

STAGE2_LOAD_SEGMENT equ 0x0000
STAGE2_LOAD_OFFSET  equ 0x8000
STAGE2_SECTORS      equ 16

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov ah, 0x02                 ; BIOS read sectors
    mov al, STAGE2_SECTORS       ; sector count
    mov ch, 0x00                 ; cylinder 0
    mov cl, 0x02                 ; start at sector 2 (sector 1 is this boot sector)
    mov dh, 0x00                 ; head 0
    mov dl, [boot_drive]
    mov bx, STAGE2_LOAD_OFFSET   ; ES:BX destination (ES is 0)
    int 0x13
    jc disk_error

    jmp STAGE2_LOAD_SEGMENT:STAGE2_LOAD_OFFSET

disk_error:
    cli
.hang:
    hlt
    jmp .hang

boot_drive: db 0

times 510 - ($ - $$) db 0
dw 0xAA55
