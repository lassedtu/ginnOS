; src/kernel/main.asm
; 32-bit kernel entry called by stage2.
;
; stage2 copies kernel.bin to physical 0x10000 and calls it like a C
; function, kernel_entry(boot_info_t *boot), with paging off. the kernel is
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

section .boot.text progbits alloc exec
global _start

_start:
    mov edx, [esp + 4]          ; boot_info_t physical address from stage2

    mov eax, cr4
    or eax, CR4_PSE
    mov cr4, eax

    mov eax, boot_page_directory ; .boot is linked at its physical address
    mov cr3, eax

    mov eax, cr0
    or eax, CR0_PG
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
    ; (edx holds the boot_info address; rep stosb uses eax, ecx and edi.)
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

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
stack_bottom:
    resb 4096
stack_top:
