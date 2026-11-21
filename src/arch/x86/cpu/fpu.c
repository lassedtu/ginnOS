#include "arch/arch.h"

#include "common/memory.h"

/**
 * @file fpu.c
 * @brief the FPU (x87) and SSE register state of user programs.
 *
 * the kernel itself never uses these registers, so a user program's state
 * stays in the CPU while it is in the kernel. the scheduler saves it when
 * it switches away and loads the next program's state, every time (no lazy
 * switching). FXSAVE/FXRSTOR keep the x87 and SSE registers in 512 bytes;
 * a CPU without them uses FNSAVE/FRSTOR (108 bytes, no SSE).
 */

#define CR0_MP 0x00000002u // WAIT honors TS
#define CR0_EM 0x00000004u // no FPU: emulate (must be clear)
#define CR0_TS 0x00000008u // task switched (lazy switching; kept clear)
#define CR0_NE 0x00000020u // FPU errors as exceptions, not IRQ 13

#define CR4_OSFXSR     0x00000200u // FXSAVE/FXRSTOR and SSE allowed
#define CR4_OSXMMEXCPT 0x00000400u // SSE errors as exceptions

#define CPUID_EDX_FXSR 0x01000000u
#define CPUID_EDX_SSE  0x02000000u

// the valid MXCSR bits (no DAZ: not every CPU has it)
#define MXCSR_VALID 0x0000FFBFu
#define FXSAVE_MXCSR_OFFSET 24

static bool use_fxsave;

// the state of a fresh program (after FNINIT)
static uint8_t clean_state[ARCH_FPU_STATE_SIZE] __attribute__((aligned(16)));

static void cpuid(uint32_t leaf, uint32_t *edx)
{
    uint32_t eax = leaf;
    uint32_t ebx;
    uint32_t ecx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(*edx));
}

void arch_fpu_init(void)
{
    uint32_t features;
    cpuid(1, &features);
    use_fxsave = (features & CPUID_EDX_FXSR) != 0;

    uintptr_t cr0; // control registers are 64 bits wide on x86_64
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 = (cr0 & ~(CR0_EM | CR0_TS)) | CR0_MP | CR0_NE;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    if (use_fxsave)
    {
        uintptr_t cr4;
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= CR4_OSFXSR;
        if (features & CPUID_EDX_SSE)
        {
            cr4 |= CR4_OSXMMEXCPT;
        }
        __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    }

    __asm__ volatile("fninit");
    arch_fpu_save(clean_state);
}

void arch_fpu_save(void *area)
{
    if (use_fxsave)
    {
        __asm__ volatile("fxsave (%0)" : : "r"(area) : "memory");
    }
    else
    {
        // FNSAVE also resets the FPU; load the state back so it stays live
        __asm__ volatile("fnsave (%0)\n\tfrstor (%0)" : : "r"(area) : "memory");
    }
}

void arch_fpu_restore(const void *area)
{
    if (use_fxsave)
    {
        __asm__ volatile("fxrstor (%0)" : : "r"(area) : "memory");
    }
    else
    {
        __asm__ volatile("frstor (%0)" : : "r"(area) : "memory");
    }
}

void arch_fpu_clean(void *area)
{
    memcpy(area, clean_state, ARCH_FPU_STATE_SIZE);
}

void arch_fpu_sanitize(void *area)
{
    // a reserved MXCSR bit set by user code would fault in FXRSTOR
    if (use_fxsave)
    {
        uint32_t *mxcsr = (uint32_t *)((uint8_t *)area + FXSAVE_MXCSR_OFFSET);
        *mxcsr &= MXCSR_VALID;
    }
}
