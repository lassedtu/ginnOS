; src/arch/x86/boot/multiboot2_header.asm
; the multiboot 2 header. it lets a multiboot 2 bootloader (limine, grub)
; load kernel.elf.
;
; the specification requires the header in the first 32 KiB of the file,
; aligned to 8 bytes. the linker puts .boot.header after the entry code in
; .boot (linker/kernel.ld), so the header is near the start of the file. the
; entry code stays at the first byte, because stage2 calls the first byte of
; kernel.bin.

BITS 32

MB2_MAGIC           equ 0xE85250D6
MB2_ARCH_I386       equ 0
MB2_TAG_END         equ 0
MB2_TAG_INFO_REQ    equ 1
MB2_TAG_FRAMEBUFFER equ 5
MB2_TAG_OPTIONAL    equ 1

; info tags the kernel reads (multiboot2.c)
MB2_INFO_CMDLINE    equ 1
MB2_INFO_MODULE     equ 3
MB2_INFO_MMAP       equ 6
MB2_INFO_FB         equ 8

section .boot.header progbits alloc noexec nowrite align=8

align 8
mb2_header_start:
    dd MB2_MAGIC
    dd MB2_ARCH_I386
    dd mb2_header_end - mb2_header_start
    dd -(MB2_MAGIC + MB2_ARCH_I386 + (mb2_header_end - mb2_header_start))

    ; the kernel can boot without these tags, so the request is optional
align 8
    dw MB2_TAG_INFO_REQ
    dw MB2_TAG_OPTIONAL
    dd 8 + 4 * 4
    dd MB2_INFO_MMAP
    dd MB2_INFO_FB
    dd MB2_INFO_MODULE
    dd MB2_INFO_CMDLINE

    ; a linear framebuffer, the same mode that stage2 asks for. if the
    ; bootloader cannot set it, the kernel uses the VGA text console.
align 8
    dw MB2_TAG_FRAMEBUFFER
    dw MB2_TAG_OPTIONAL
    dd 20
    dd 1280
    dd 1024
    dd 32

align 8
    dw MB2_TAG_END
    dw 0
    dd 8
mb2_header_end:
