[BITS 16] ; 16-bit real mode
[ORG 0x7c00] ; boot sector is loaded at memory address 0

CODE_OFFSET equ 0x8
DATA_OFFSET equ 0x10

; bootloader entry point
start:
    cli             ; disable interrupts
    mov ax, 0x00    ; zero the segment register
    mov ds, ax      ; zero the data segment register
    mov es, ax      ; zero the extra segment register
    mov ss, ax      ; zero the stack segment register
    mov sp, 0x7c00  ; point stack to the boot sector
    sti             ; enable interrupts again


; switch to protected mode
load_pm:
    cli                         ; disable interrupts
    lgdt [gdt_descriptor]       ; load the GDT descriptor into GDTR
    mov eax, cr0                ; load the control register
    or eax, 1                   ; set the PE (Protection Enable) bit
    mov cr0, eax                ; write back to control register
    jmp CODE_OFFSET:PModeMain   ; jump to protected mode code


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
    mov ax, DATA_OFFSET ; load data segment selector
    mov ds, ax          ; set data segment register
    mov es, ax          ; set extra segment register
    mov fs, ax          ; set fs segment register
    mov ss, ax          ; set stack segment register
    mov gs, ax          ; set gs segment register
    mov esp, 0x9c00     ; set stack pointer to a safe location
    mov esp, ebp        ; set stack pointer to the end of the bootloader code

    in al, 0x92         ; read the current value of the port 0x92
    or al, 2            ; set the second bit to enable A20 line
    out 0x92, al        ; write the modified value back to port 0x92

    jmp $               ; infinite loop to prevent the CPU from executing random instructions


; fill the rest of the boot sector with zeros so the total size is 510 bytes
times 510 - ($ - $$) db 0

dw 0xAA55 ; boot signature (0xAA55) to indicate a valid boot sector