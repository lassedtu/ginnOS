; Stage2 loader:
;   set up segments & stack
;   switch to 32-bit protected mode
;   populate boot_info_t
;   call cstart_() in C to load kernel from EXT2 filesystem and execute it

BITS 16

%define BOOT_INFO_BOOT_DRIVE            0
%define BOOT_INFO_MEMORY_MAP_COUNT      4
%define BOOT_INFO_MEMORY_MAP_REGIONS    8
%define MEMORY_REGION_SIZE              20
%define MEMORY_MAP_MAX_REGIONS          32
%define BOOT_INFO_TOTAL_SIZE            (BOOT_INFO_MEMORY_MAP_REGIONS + MEMORY_MAP_MAX_REGIONS * MEMORY_REGION_SIZE)
%define E820_SIGNATURE                  0x534D4150

section .text
global _start
extern cstart_

; Stage1 jumps to linear address 0x8000 (start of stage2 binary), not ELF e_entry.
; Keep an explicit jump here so raw binary execution always lands in _start.
jmp _start

collect_e820:
    mov dword [boot_info + BOOT_INFO_MEMORY_MAP_COUNT], 0
    xor ebx, ebx
    xor esi, esi

.next_entry:
    cmp esi, MEMORY_MAP_MAX_REGIONS
    jae .done

    mov di, boot_info + BOOT_INFO_MEMORY_MAP_REGIONS
    mov ax, si
    mov cx, MEMORY_REGION_SIZE
    mul cx
    add di, ax

    xor ax, ax
    mov es, ax

    mov eax, 0xE820
    mov edx, E820_SIGNATURE
    mov ecx, MEMORY_REGION_SIZE

    sti
    int 0x15
    cli

    jc .done
    cmp eax, E820_SIGNATURE
    jne .done

    inc esi
    mov dword [boot_info + BOOT_INFO_MEMORY_MAP_COUNT], esi

    test ebx, ebx
    jnz .next_entry

.done:
    ret

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

    mov [boot_info + BOOT_INFO_BOOT_DRIVE], dl
    call collect_e820

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

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

align 4
boot_info: times BOOT_INFO_TOTAL_SIZE db 0
