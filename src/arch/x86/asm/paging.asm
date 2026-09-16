; paging assembly helpers for x86.
; keeps every raw control-register access in one place so the portable
; paging.c never has to reach for inline assembly. provides CR3 load/switch,
; CR0 read, TLB invalidation, and CR2 read.

BITS 32

global paging_flush
global paging_invalidate
global paging_read_cr2
global paging_read_cr0
global paging_load_cr3


; void paging_flush(uint32_t page_directory_phys)
; loads the page directory address into CR3 and enables paging (CR0.PG).

paging_flush:

    mov eax, [esp + 4]  ; page directory physical address

    mov cr3, eax        ; load page directory into CR3

    mov eax, cr0        ; read current CR0
    or  eax, 0x80000000 ; set PG bit (bit 31)
    mov cr0, eax        ; enable paging

    ret


; void paging_invalidate(uint32_t virtual_address)
; invalidates the TLB entry for the given virtual address.

paging_invalidate:

    mov eax, [esp + 4]  ; virtual address to invalidate

    invlpg [eax]        ; flush the TLB entry

    ret


; uint32_t paging_read_cr2(void)
; returns the value of CR2 (faulting linear address on page fault).

paging_read_cr2:

    mov eax, cr2        ; read faulting address

    ret


; uint32_t paging_read_cr0(void)
; returns the current value of CR0 (so callers can test the PG bit).

paging_read_cr0:

    mov eax, cr0        ; read current CR0

    ret


; void paging_load_cr3(uint32_t page_directory_phys)
; swaps the active page directory without touching CR0. use this to change
; address spaces once paging is already on; paging_flush is only for the
; initial enable, since it also sets the PG bit.

paging_load_cr3:

    mov eax, [esp + 4]  ; page directory physical address

    mov cr3, eax        ; load page directory into CR3

    ret
