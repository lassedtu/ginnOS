; src/kernel/main.asm
; 32-bit kernel entry. two bootloaders can start the kernel:
;   - stage2 copies kernel.bin to physical 0x10000 and calls it like a C
;     function, kernel_entry(boot_info_t *boot).
;   - a multiboot 2 bootloader (limine) loads kernel.elf at the same address
;     and jumps here with EAX = MB2_BOOT_MAGIC and EBX = the physical address
;     of the multiboot 2 information. ESP is not set.
; both start the kernel with paging off. cstart gets the physical address
; and the magic value, and makes one boot_info_t from either source. the
; kernel is
; linked in the higher half (linker/kernel.ld), so this stub, linked at its
; physical address, turns paging on with a boot page directory before any
; higher-half code runs:
;   - PDE 0: identity 0-4 MiB, so the stub keeps running after paging is on
;   - PDE 768+: the first PHYS_MAP_SIZE (512 MiB) of RAM at 0xC0000000, so all
;     RAM the early kernel touches is reachable through phys_to_virt
; both use 4 MiB pages (CR4.PSE). paging_init later installs the real kernel
; directory (4 KiB tables, no identity map).

BITS 32

KERNEL_VIRT_BASE    equ 0xC0000000
KERNEL_PDE_START    equ KERNEL_VIRT_BASE >> 22
BOOT_MAP_PDES       equ 128          ; 128 x 4 MiB = PHYS_MAP_SIZE
PDE_4M_KERNEL_RW    equ 0x83         ; present | writable | 4 MiB page
CR4_PSE             equ 0x10
CR0_PG              equ 0x80000000
CR0_WP              equ 0x00010000   ; read-only pages are read-only to ring 0 too
MB2_BOOT_MAGIC      equ 0x36D76289   ; EAX from a multiboot 2 bootloader

section .boot.text progbits alloc exec
global _start

_start:
    ; edx = the physical address of the boot information, esi = the magic
    ; value (0 for stage2). do not use the stack before higher_half: a
    ; multiboot 2 bootloader does not give one.
    cmp eax, MB2_BOOT_MAGIC
    jne .from_stage2
    mov edx, ebx
    mov esi, eax
    jmp .paging
.from_stage2:
    mov edx, [esp + 4]          ; boot_info_t physical address from stage2
    xor esi, esi

.paging:

    mov eax, cr4
    or eax, CR4_PSE
    mov cr4, eax

    mov eax, boot_page_directory ; .boot is linked at its physical address
    mov cr3, eax

    ; with WP set, a kernel write to a read-only user page (a copy-on-write
    ; page after fork) faults instead of changing a frame another process
    ; shares. user copies go through uaccess.c, which breaks the sharing
    ; first.
    mov eax, cr0
    or eax, CR0_PG | CR0_WP
    mov cr0, eax

    ; paging is on: continue at the higher-half link address
    mov eax, higher_half
    jmp eax

section .boot.data progbits alloc write
align 4096
boot_page_directory:
    dd PDE_4M_KERNEL_RW                         ; identity 0-4 MiB
    times (KERNEL_PDE_START - 1) dd 0
%assign i 0
%rep BOOT_MAP_PDES
    dd (i << 22) | PDE_4M_KERNEL_RW             ; 0xC0000000 + i * 4 MiB
%assign i i + 1
%endrep
    times (1024 - KERNEL_PDE_START - BOOT_MAP_PDES) dd 0

section .text
extern cstart
extern __bss_start
extern __bss_end

higher_half:
    mov esp, stack_top

    ; .bss is not part of kernel.bin, so whatever stage2 or the firmware
    ; left in that memory is still there: zero it before any C runs.
    ; (edx and esi hold the boot arguments; rep stosb uses eax, ecx and
    ; edi.)
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

    push esi                    ; cstart(boot_phys, magic)
    push edx
    ; a zero frame pointer marks the outermost frame for backtraces.
    xor ebp, ebp
    call cstart

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
; the boot stack. the kernel stays on it after boot (the tests, the idle
; loop, and the interrupts that come in there), so 4 KiB was too little:
; it ran over its bottom into the last variables of .data. 16 KiB, as on
; x86_64.
stack_bottom:
    resb 16384
stack_top:
