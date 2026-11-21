/**
 * the page attribute table (Intel SDM volume 3, section 13.12).
 *
 * the PAT, PCD and PWT bits of a page table entry select one of eight
 * memory types in the IA32_PAT MSR. the power-on value maps PWT to
 * write-through. the kernel changes entries 1 and 5 (PWT set, PCD clear)
 * to write-combining: writes to the framebuffer are then combined into
 * bursts, which is much faster than uncached writes. the other entries
 * keep their power-on types, so ordinary pages do not change.
 */

#include "pat.h"

#define MSR_PAT 0x277u

// memory types
#define PAT_UC  0x00ull
#define PAT_WC  0x01ull
#define PAT_WB  0x06ull
#define PAT_UCM 0x07ull // UC-

static bool write_combining;

void pat_init(void)
{
    uint32_t eax = 1;
    uint32_t ebx;
    uint32_t ecx = 0;
    uint32_t edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    if (!(edx & (1u << 16)))
    {
        return; // no PAT
    }

    // entries 0 to 7: WB, WC, UC-, UC, WB, WC, UC-, UC
    uint64_t value = PAT_WB | (PAT_WC << 8) | (PAT_UCM << 16) | (PAT_UC << 24) | (PAT_WB << 32) |
                     (PAT_WC << 40) | (PAT_UCM << 48) | (PAT_UC << 56);
    __asm__ volatile("wrmsr" : : "c"(MSR_PAT), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));

    // the TLB may hold old memory types; reloading CR3 empties it
    uintptr_t cr3;
    __asm__ volatile("mov %%cr3, %0\n\tmov %0, %%cr3" : "=r"(cr3) : : "memory");
    write_combining = true;
}

bool pat_has_write_combining(void)
{
    return write_combining;
}
