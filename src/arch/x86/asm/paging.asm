; paging assembly helpers for x86.
; provides CR3 load, TLB invalidation, and CR2 read.

BITS 32

global paging_flush
global paging_invalidate
global paging_read_cr2


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
