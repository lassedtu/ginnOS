[BITS 16] ; 16-bit real mode
[ORG 0x7c00] ; boot sector is loaded at memory address 0

CODE_OFFSET equ 0x8
DATA_OFFSET equ 0x10

KERNEL_LOAD_SEG equ 0x1000 ; segment where the kernel will be loaded
KERNEL_START_ADDR equ 0x10000 ; physical address where the kernel will be loaded (64KB)

; bootloader entry point
start:
    ; set up the stack and segment registers
    cli             ; disable interrupts
    mov ax, 0x00    ; zero the segment register
    mov ds, ax      ; zero the data segment register
    mov es, ax      ; zero the extra segment register
    mov ss, ax      ; zero the stack segment register
    mov sp, 0x7c00  ; point stack to the boot sector
    sti             ; enable interrupts again

    ; load kernel from disk into memory
    mov ax, KERNEL_LOAD_SEG ; load segment where kernel will be loaded
    mov es, ax              ; set the kernel load segment
    xor bx, bx              ; load at offset 0 within that segment
    mov dh, 0x00            ; head number (0 for first head)
    mov dl, 0x80            ; drive number (0x80 for first hard disk)
    mov cl, 0x02            ; sector number (2 for the first sector of the kernel)
    mov ch, 0x00            ; cylinder number (0 for the first cylinder)
    mov ah, 0x02            ; BIOS function to read sectors from disk
    mov al, 18              ; number of sectors to read (18 sectors = 9KB)
    int 0x13                ; call BIOS interrupt to read sectors
    
    jc disk_read_error      ; jump to error handler if disk read fails

; switch to protected mode
load_pm:
    cli                         ; disable interrupts
    lgdt [gdt_descriptor]       ; load the GDT descriptor into GDTR
    mov eax, cr0                ; load the control register
    or eax, 1                   ; set the PE (Protection Enable) bit
    mov cr0, eax                ; write back to control register
    jmp CODE_OFFSET:PModeMain   ; jump to protected mode code

disk_read_error:
    hlt ; halt the CPU if disk read fails

; gdt implementation
gdt_start:
    dd 0x00000000 ; null descriptor
    dd 0x00000000

    ; code segment descriptor
    dw 0xFFFF       ; limit low
    dw 0x0000       ; base low
    db 0x00         ; base middle
    db 10011010b    ; access byte (present, ring 0, code segment, executable, readable)
    db 11001111b    ; flags (granularity, 32-bit)
    db 0x00         ; base high

    ; data segment descriptor
    dw 0xFFFF       ; limit low
    dw 0x0000       ; base low
    db 0x00         ; base middle
    db 10010010b    ; access byte (present, ring 0, data segment, writable)
    db 11001111b    ; flags (granularity, 32-bit)
    db 0x00         ; base high

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1 ; size of gdt (limit)
    dd gdt_start               ; base address of gdt

[BITS 32] ; 32-bit protected mode
PModeMain:
    mov ax, DATA_OFFSET                 ; load data segment selector
    mov ds, ax                          ; set data segment register
    mov es, ax                          ; set extra segment register
    mov fs, ax                          ; set fs segment register
    mov ss, ax                          ; set stack segment register
    mov gs, ax                          ; set gs segment register
    mov esp, 0x9c00                     ; set stack pointer to a safe location

    jmp CODE_OFFSET:KERNEL_START_ADDR   ; jump to the kernel entry point in protected mode


; fill the rest of the boot sector with zeros so the total size is 510 bytes
times 510 - ($ - $$) db 0

dw 0xAA55 ; boot signature (0xAA55) to indicate a valid boot sector