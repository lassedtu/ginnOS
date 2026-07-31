[BITS 16] ; 16-bit real mode
[ORG 0x7c00] ; boot sector is loaded at memory address 0

; bootloader entry point
start:
    cli ; disable interrupts
    mov ax, 0x00 ; zero the segment register
    mov ds, ax ; zero the data segment register
    mov es, ax ; zero the extra segment register
    mov ss, ax ; zero the stack segment register
    mov sp, 0x7c00 ; point stack to the boot sector
    sti ; enable interrupts again
    mov si, msg ; load the message string address into SI


print: ; print the message character by character
    lodsb ; load byte at adress DS:SI into AL and increment SI
    cmp al, 0 ; check for null terminator
    je done ; if null terminator, jump to done
    mov ah, 0x0e ; BIOS teletype function
    int 0x10 ; call BIOS interrupt to print character in AL
    jmp print ; unconditional jump to print (repeat for next character)

done:
    cli ; disable interrupts
    hlt ; halt the CPU

msg: db 'Hello World!', 0 ; message to display

; fill the rest of the boot sector with zeros so the total size is 512 bytes
times 510 - ($ - $$) db 0

dw 0xAA55 ; boot signature (0xAA55) to indicate a valid boot sector