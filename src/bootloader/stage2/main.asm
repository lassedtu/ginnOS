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

; framebuffer fields, written by set_video_mode below. offsets match
; boot_info_t (locked by _Static_assert in memory.c). the memory map ends at
; offset 8 + 32*20 = 648.
%define BOOT_INFO_FB_ADDR               648
%define BOOT_INFO_FB_WIDTH              652
%define BOOT_INFO_FB_HEIGHT             656
%define BOOT_INFO_FB_CMDLINE            660
%define BOOT_INFO_FB_PITCH              664
%define BOOT_INFO_FB_BPP                668
%define BOOT_INFO_FB_TYPE              669
%define BOOT_INFO_FB_RED_SIZE           670
%define BOOT_INFO_FB_RED_SHIFT          671
%define BOOT_INFO_FB_GREEN_SIZE         672
%define BOOT_INFO_FB_GREEN_SHIFT        673
%define BOOT_INFO_FB_BLUE_SIZE          674
%define BOOT_INFO_FB_BLUE_SHIFT         675

; trailing arch-neutral fields the BIOS boot path zeroes by default. covers
; framebuffer_addr..cmdline plus the pixel-format block and 2 pad bytes:
; 4+4+4+4 (addr,width,height,cmdline) + 4 (pitch) + 1+1 (bpp,type)
; + 6 (channel size/shift) + 2 (pad) = 28 bytes.
%define BOOT_INFO_TRAILER_SIZE          28
%define BOOT_INFO_TOTAL_SIZE            (BOOT_INFO_MEMORY_MAP_REGIONS + MEMORY_MAP_MAX_REGIONS * MEMORY_REGION_SIZE + BOOT_INFO_TRAILER_SIZE)
%define E820_SIGNATURE                  0x534D4150

; VBE constants
%define VBE_MODE_INFO_BUF               0x7000  ; scratch for the 256-byte mode info block
%define VBE_LFB_ATTR                    0x90    ; mode attrs: bit4 graphics, bit7 linear fb
%define VBE_LFB_ENABLE                  0x4000  ; bit14: use linear framebuffer

section .text
global _start
extern cstart_

; Stage1 jumps to linear address 0x8000 (start of stage2 binary), so _start
; must be the very first code. keeping it first (rather than a forward 'jmp
; _start' over the helper routines) avoids nasm pass-convergence errors as the
; entry jump's size would otherwise change when helpers grow.
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
    call set_video_mode

    ; Switch to 32-bit protected mode
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax
    jmp 0x08:protected_mode_entry

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

; set_video_mode: try a short list of 32bpp linear VBE modes, and on success
; record the framebuffer geometry and pixel format in boot_info. on any
; failure the boot_info framebuffer fields stay zero and we remain in text
; mode, so the kernel falls back to the VGA text console.
;
; VBE mode-info block fields we read (offsets into the 256-byte block):
;   +0  mode_attributes (word)   bit4 = graphics, bit7 = linear fb available
;   +16 bytes_per_scanline (word)
;   +18 x_resolution (word)
;   +20 y_resolution (word)
;   +25 bits_per_pixel (byte)
;   +31 red_mask_size,  +32 red_field_position
;   +33 green_mask_size,+34 green_field_position
;   +35 blue_mask_size, +36 blue_field_position
;   +40 phys_base_ptr (dword)  linear framebuffer physical address
set_video_mode:
    pusha
    mov si, vbe_mode_list

.try_next:
    mov cx, [si]            ; candidate VBE mode number
    cmp cx, 0xFFFF          ; end-of-list sentinel
    je .fail
    add si, 2
    mov bp, cx              ; keep the mode number; int 0x10 may clobber cx

    ; get mode info into VBE_MODE_INFO_BUF (needs mode in cx, buffer in es:di)
    push si
    xor ax, ax
    mov es, ax
    mov di, VBE_MODE_INFO_BUF
    mov ax, 0x4F01
    sti
    int 0x10
    cli
    pop si

    cmp ax, 0x004F          ; AL=4F supported, AH=00 success
    jne .try_next

    ; require graphics + linear framebuffer (attrs bit4 and bit7)
    mov ax, [VBE_MODE_INFO_BUF + 0]
    and ax, VBE_LFB_ATTR
    cmp ax, VBE_LFB_ATTR
    jne .try_next

    ; accept 24- or 32-bit direct-colour modes. the kernel fb driver uses the
    ; recorded bpp and channel masks, so it handles either packing.
    mov al, [VBE_MODE_INFO_BUF + 25]
    cmp al, 32
    je .accept
    cmp al, 24
    jne .try_next
.accept:

    ; activate the mode with the linear-framebuffer bit set
    mov bx, bp
    or bx, VBE_LFB_ENABLE
    mov ax, 0x4F02
    sti
    int 0x10
    cli
    cmp ax, 0x004F
    jne .try_next

    ; success: copy geometry + pixel format into boot_info
    xor eax, eax
    mov ax, [VBE_MODE_INFO_BUF + 18]        ; width
    mov [boot_info + BOOT_INFO_FB_WIDTH], eax
    mov ax, [VBE_MODE_INFO_BUF + 20]        ; height
    mov [boot_info + BOOT_INFO_FB_HEIGHT], eax
    mov ax, [VBE_MODE_INFO_BUF + 16]        ; pitch (bytes per scanline)
    mov [boot_info + BOOT_INFO_FB_PITCH], eax
    mov eax, [VBE_MODE_INFO_BUF + 40]       ; framebuffer physical address
    mov [boot_info + BOOT_INFO_FB_ADDR], eax

    mov al, [VBE_MODE_INFO_BUF + 25]        ; bpp
    mov [boot_info + BOOT_INFO_FB_BPP], al
    mov byte [boot_info + BOOT_INFO_FB_TYPE], 1  ; 1 = RGB linear framebuffer

    mov al, [VBE_MODE_INFO_BUF + 31]        ; red size
    mov [boot_info + BOOT_INFO_FB_RED_SIZE], al
    mov al, [VBE_MODE_INFO_BUF + 32]        ; red shift
    mov [boot_info + BOOT_INFO_FB_RED_SHIFT], al
    mov al, [VBE_MODE_INFO_BUF + 33]        ; green size
    mov [boot_info + BOOT_INFO_FB_GREEN_SIZE], al
    mov al, [VBE_MODE_INFO_BUF + 34]        ; green shift
    mov [boot_info + BOOT_INFO_FB_GREEN_SHIFT], al
    mov al, [VBE_MODE_INFO_BUF + 35]        ; blue size
    mov [boot_info + BOOT_INFO_FB_BLUE_SIZE], al
    mov al, [VBE_MODE_INFO_BUF + 36]        ; blue shift
    mov [boot_info + BOOT_INFO_FB_BLUE_SHIFT], al

.fail:
    popa
    ret

; preferred 32bpp linear modes, highest first, terminated by 0xFFFF.
; 0x118 = 1024x768x32, 0x115 = 800x600x32, 0x112 = 640x480x32.
vbe_mode_list:
    dw 0x118
    dw 0x115
    dw 0x112
    dw 0xFFFF

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
