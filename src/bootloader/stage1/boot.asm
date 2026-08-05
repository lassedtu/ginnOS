; 16-bit boot sector loaded by BIOS at 0x7C00.
; loads stage2 from disk sectors 1..62 and jumps to it.

BITS 16
ORG 0x7C00

STAGE2_LOAD_SEGMENT equ 0x0000
STAGE2_LOAD_OFFSET  equ 0x8000
STAGE2_SECTORS      equ 62

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
    mov al, STAGE2_SECTORS       ; sector count (62 sectors)
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

times 446 - ($ - $$) db 0

; MBR Partition Table (offset 446 / 0x1BE)
; Partition 1: Active (0x80), Linux native (0x83), LBA start = 63
db 0x80              ; boot indicator (active)
db 0x00, 0x01, 0x01  ; CHS start
db 0x83              ; partition type
db 0x00, 0x3F, 0x20  ; CHS end
dd 63                ; LBA start sector
dd 32768             ; partition sector count

times 16 * 3 db 0    ; Partitions 2, 3, 4 (unused)

dw 0xAA55            ; Boot signature
